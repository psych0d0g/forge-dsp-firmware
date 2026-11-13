/*
 * PORT extension (#61): backup to the microSD before a firmware update. The firmware copies
 * what an update can touch to the card itself, one part per command, and reads each file back:
 *   set system.sd.backup.flash=S    FLASH_S.BIN   application section (APP_BYTES)
 *   set system.sd.backup.eeprom=S   EEPROM_S.BIN  MCU EEPROM (4 KB)
 *   set system.sd.backup.preset=S   PRESET_S.BIN  preset EEPROM 24xx512, bus 1 0xA8 (64 KB)
 *   set system.sd.backup.N=S        DSPN_S.BIN    EEPROM of DSP core N (program and biquads, 64 KB)
 *   get system.sd.backup            = [flash bytes, EEPROM bytes, preset EEPROM bytes (0: none),
 *                                      DSP cores]
 * S = slot 1..9. The answer comes after the read back: ok when the file has the size and the
 * CRC-16 of what was read from the memory, otherwise an error (the file stays, damaged).
 * The main loop waits meanwhile (flash and MCU EEPROM a few seconds, a 64 KB EEPROM on I2C
 * about 6 s at 100 kHz), the audio keeps running.
 * License: CC BY-NC-SA 4.0
 */
#include <string.h>
#include "cmd.h"
#include "eram.h"
#include "ff.h"
#include "flavour.h"
#include "i2c.h"
#include "iface.h"
#include "sd.h"
#include "store.h"

#ifdef __AVR__
#include <avr/io.h>
#include <avr/pgmspace.h>
#define APP_BYTES ((uint32_t)APP_SECTION_SIZE)
#define EE_BYTES  ((uint16_t)(E2END + 1))
static uint8_t flash_byte(uint32_t a) { return pgm_read_byte_far(a); }
#else
#define APP_BYTES 0x40000UL
#define EE_BYTES  4096u
uint8_t host_flash_byte(uint32_t a);   /* tests/native/host_iface.c */
static uint8_t flash_byte(uint32_t a) { return host_flash_byte(a); }
#endif
#define I2C_EE_BYTES 0x10000UL

#define W_FLASH  0x666C
#define W_EEPROM 0x6565
#define W_PRESET 0x7072   /* "at24" would not do: the parser splits a trailing number off */

static uint8_t buf[128];
static uint8_t src_bus, src_dev;   /* I2C EEPROM of the running part */

static uint8_t ee_present(uint8_t bus, uint8_t dev)
{
	if (!(channel_open & (1u << (4 + bus))))
		return 0;   /* bus not opened (safe variant) */
	twi_start(bus);
	twi_wbyte(bus, dev);
	uint8_t ok = i2c_err == 0;
	twi_stop(bus);
	return ok;
}

/* n bytes from offset a of the part into buf; 0 = the memory did not answer */
static uint8_t (*read_part)(uint32_t a, uint8_t n);
static uint8_t read_flash(uint32_t a, uint8_t n)
{
	for (uint8_t i = 0; i < n; i++)
		buf[i] = flash_byte(a + i);
	return 1;
}
static uint8_t read_eeprom(uint32_t a, uint8_t n)
{
	for (uint8_t i = 0; i < n; i++)
		buf[i] = eram_hw_read((uint16_t)(a + i));
	return 1;
}
static uint8_t read_i2c(uint32_t a, uint8_t n)
{
	twi_start(src_bus);
	twi_wbyte(src_bus, src_dev);
	twi_wbyte(src_bus, (uint8_t)(a >> 8));
	twi_wbyte(src_bus, (uint8_t)a);
	twi_repstart(src_bus);
	twi_wbyte(src_bus, (uint8_t)(src_dev + 1));
	uint8_t ok = i2c_err == 0;
	for (uint8_t i = 0; i < n; i++)
		buf[i] = twi_rbyte(src_bus, i + 1 < n);
	twi_stop(src_bus);
	return ok;
}

/* the part to the card and back; 0 = ok */
static uint8_t copy(const char *name, uint32_t size)
{
	static FIL f;
	UINT bw;
	uint16_t crc = 0;
	if (f_open(&f, name, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
		return 1;
	uint8_t err = 0;
	for (uint32_t a = 0; a < size && !err; a += sizeof buf) {
		if (!read_part(a, sizeof buf))
			err = 2;
		else if (f_write(&f, buf, sizeof buf, &bw) != FR_OK || bw != sizeof buf)
			err = 3;
		crc = bas_crc16_more(crc, buf, sizeof buf);
		if ((a & 0xFFF) == 0)
			iface_hook_watchdog();
	}
	if (f_close(&f) != FR_OK && !err)
		err = 3;
	if (err)
		return err;
	/* read back */
	uint16_t back = 0;
	uint32_t n = 0;
	if (f_open(&f, name, FA_READ) != FR_OK)
		return 4;
	while (f_read(&f, buf, sizeof buf, &bw) == FR_OK && bw) {
		back = bas_crc16_more(back, buf, (uint16_t)bw);
		n += bw;
		if ((n & 0xFFF) == 0)
			iface_hook_watchdog();
	}
	f_close(&f);
	return n == size && back == crc ? 0 : 4;
}

static uint8_t cores(void)
{
#ifdef BAS_MODUL_TAS3108_CORES
	return BAS_MODUL_TAS3108_CORES;
#else
	return 0;
#endif
}

void sd_backup_get(void)
{
	cmd_answer_header();
	out_chr('[');
	out_u32(APP_BYTES); out_chr(',');
	out_u32(EE_BYTES); out_chr(',');
	out_u32((hardware_other & (1u << BAS_AT24C512_STORE_RDY)) ? I2C_EE_BYTES : 0); out_chr(',');
	out_u16(cores());
	out_chr(']');
	out_crlf();
}

void sd_backup_set(void)
{
	int16_t slot = cmd_reg_var_integers[1];
	uint16_t w4 = req_w(cmd_req_word_four);
	char name[13];
	uint32_t size = I2C_EE_BYTES;
	if (slot < 1 || slot > 9) {
		cmd_error_cmd_oor();
		return;
	}
	if (w4 == W_FLASH) {
		strcpy(name, "FLASH_x.BIN"); read_part = read_flash; size = APP_BYTES;
	} else if (w4 == W_EEPROM) {
		strcpy(name, "EEPROM_x.BIN"); read_part = read_eeprom; size = EE_BYTES;
	} else if (w4 == W_PRESET) {
		strcpy(name, "PRESET_x.BIN"); read_part = read_i2c; src_bus = 1; src_dev = 0xA8;
	} else if (REQ_B(10) == cmd_req_number && REQ_B(9) >= 1 && REQ_B(9) <= 3) {
		uint8_t core = REQ_B(9);
		if (core > cores()) {
			cmd_error_cmd_oor();
			return;
		}
		strcpy(name, "DSPx_x.BIN"); name[3] = (char)('0' + core); read_part = read_i2c;
		src_bus = core == 1 ? 1 : 2; src_dev = core == 3 ? 0xA2 : 0xA0;   /* tas3108.h */
	} else {
		cmd_error_unknown();
		return;
	}
	strchr(name, '_')[1] = (char)('0' + slot);
	if (read_part == read_i2c && !ee_present(src_bus, src_dev)) {
		cmd_error_cmd_oor();
		return;
	}
	if (!sd_mounted() || copy(name, size)) {
		cmd_error_unknown();
		return;
	}
	cmd_answer_ok();
}
