/*
 * Port of libs/dsp/tas3108/at24c512/{1,2,3} and cores/{1,2,3}/init.bas, plus
 * i2c{1,2}_sda_wait4silence (libs/interface/V40/5,6).
 * The DSPs load their program from their own EEPROM after the reset is released. At every
 * start the firmware compares the byte sum of that EEPROM with the program in its flash
 * (dsp_N_code, $inc of the processflows .bin files) and rewrites the EEPROM if they differ. The port
 * carries the same programs byte for byte, so on a device with the original firmware the
 * sums are equal and nothing is written.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "flavour.h"
#include "bas_data.h"
#include "eram.h"
#include "flavour_data.h"
#include "i2c.h"
#include "registers.h"
#include "tas3108.h"

uint16_t hardware_dsp;

struct dsp_def {
	uint8_t bus, eeprom_adr, core_adr, eeprom_rdy, core_rdy;
	const uint8_t *code;
	uint16_t chksum_eram;
};

static const struct dsp_def dsps[] = {
#if BAS_MODUL_TAS3108_CORES > 0
	{ 1, 0xA0, 0x68, BAS_AT24C512_1_RDY, BAS_TAS3108_1_RDY, dsp_1_code, ERAM_DSP_1_EEPROM_CHKSUM_ERAM },
#endif
#if BAS_MODUL_TAS3108_CORES > 1
	{ 2, 0xA0, 0x68, BAS_AT24C512_2_RDY, BAS_TAS3108_2_RDY, dsp_2_code, ERAM_DSP_2_EEPROM_CHKSUM_ERAM },
#endif
#if BAS_MODUL_TAS3108_CORES > 2
	{ 2, 0xA2, 0x6A, BAS_AT24C512_3_RDY, BAS_TAS3108_3_RDY, dsp_2_code, ERAM_DSP_3_EEPROM_CHKSUM_ERAM },
#endif
};

static uint16_t data_size(const struct dsp_def *d)
{
	return bas_data_word(d->code);  /* restore dsp_N_code : read dsp_N_eeprom_size */
}

static uint16_t data_get_chksum(const struct dsp_def *d)
{
	uint16_t sum = 0;
	if (registers[BAS_REG_STARTUP_STATE] == BAS_STARTUP_STATE_SYSINIT) {
		uint16_t n = data_size(d);
		for (uint16_t i = 0; i < n; i++)
			sum = (uint16_t)(sum + bas_data_byte(d->code + 2 + i));
		eram_ww(d->chksum_eram, sum);
	} else {
		sum = eram_rw(d->chksum_eram);
	}
	return sum;
}

static uint16_t eeprom_get_chksum(const struct dsp_def *d, uint16_t size)
{
	uint16_t sum = 0;
	if (size == 0)
		return 0;
	twi_start(d->bus);
	twi_wbyte(d->bus, d->eeprom_adr);
	twi_wbyte(d->bus, 0x00);
	twi_wbyte(d->bus, 0x00);
	twi_stop(d->bus);
	twi_start(d->bus);
	twi_wbyte(d->bus, (uint8_t)(d->eeprom_adr + 1));  /* current address read */
	for (uint16_t i = 2; i <= size; i++)
		sum = (uint16_t)(sum + twi_rbyte(d->bus, 1));
	sum = (uint16_t)(sum + twi_rbyte(d->bus, 0));
	twi_stop(d->bus);
	return sum;
}

static void eeprom_restart(const struct dsp_def *d, uint16_t a)
{
	twi_stop(d->bus);
	delay_ms(BAS_EEPROM_PAGE_WRITE_DELAY);
	twi_start(d->bus);
	twi_wbyte(d->bus, d->eeprom_adr);
	twi_wbyte(d->bus, (uint8_t)(a >> 8));
	twi_wbyte(d->bus, (uint8_t)(a & 0xFF));
}

static void eeprom_clear_mem(const struct dsp_def *d)
{
	twi_start(d->bus);
	twi_wbyte(d->bus, d->eeprom_adr);
	twi_wbyte(d->bus, 0x00);
	twi_wbyte(d->bus, 0x00);
	uint8_t page = 0;
	for (uint16_t count = 0; count <= BAS_DSP_EEPROM_CLEAR_COUNT; count++) {
		twi_wbyte(d->bus, 0xFF);
		page++;
		if (page > BAS_DSP_EEPROM_PAGE_SIZE) {
			eeprom_restart(d, (uint16_t)(count + 1));
			page = 0;
		}
	}
	twi_stop(d->bus);
	delay_ms(BAS_EEPROM_PAGE_WRITE_DELAY);
}

static void eeprom_copy_data(const struct dsp_def *d)
{
	uint16_t size = data_size(d);
	if (size == 0)
		return;
	twi_start(d->bus);
	twi_wbyte(d->bus, d->eeprom_adr);
	twi_wbyte(d->bus, 0x00);
	twi_wbyte(d->bus, 0x00);
	uint8_t page = 0;
	for (uint16_t count = 1; count <= size; count++) {
		twi_wbyte(d->bus, bas_data_byte(d->code + 1 + count));
		page++;
		if (page > BAS_DSP_EEPROM_PAGE_SIZE) {
			eeprom_restart(d, count);  /* next byte goes to address count */
			page = 0;
		}
	}
	twi_stop(d->bus);
	delay_ms(BAS_EEPROM_PAGE_WRITE_DELAY);
}

void dsp_eeprom_init(uint8_t n)
{
	if (n < 1 || n > sizeof dsps / sizeof dsps[0])
		return;
	const struct dsp_def *d = &dsps[n - 1];
	if (channel_open & (1u << (4 + d->bus))) {
		twi_start(d->bus);
		twi_wbyte(d->bus, d->eeprom_adr);
		if (i2c_err == 0)
			hardware_dsp |= 1u << d->eeprom_rdy;
		else
			errorcount((uint8_t)(d->eeprom_rdy + BAS_ERRORCOUNT_OFFSET_DSP));
		twi_stop(d->bus);
	}
	if (!(hardware_dsp & (1u << d->eeprom_rdy)))
		return;
	uint16_t size = data_size(d);
	uint16_t data_chksum = data_get_chksum(d);
	uint16_t eeprom_chksum = eeprom_get_chksum(d, size);
	if (data_chksum != eeprom_chksum) {
		eeprom_clear_mem(d);
		eeprom_copy_data(d);
	}
}

void dsp_core_init(uint8_t n)
{
	if (n < 1 || n > sizeof dsps / sizeof dsps[0])
		return;
	const struct dsp_def *d = &dsps[n - 1];
	if (channel_open & (1u << (4 + d->bus))) {
		twi_start(d->bus);
		twi_wbyte(d->bus, d->core_adr);
		if (i2c_err == 0)
			hardware_dsp |= 1u << d->core_rdy;
		else
			errorcount((uint8_t)(d->core_rdy + BAS_ERRORCOUNT_OFFSET_DSP));
		twi_stop(d->bus);
	}
	if (!(hardware_dsp & (1u << d->core_rdy)))
		return;
	/* dsp_N_get_version, dsp_N_get_state: buffer_bytes(4..1) */
	i2c_device_addr = d->core_adr;
	i2c_reg = BAS_DSP_SUB_VERSION;
	i2c_size = BAS_DSP_REG_SIZE_VERSION;
	i2c_rbuffer(d->bus);
	i2c_device_addr = d->core_adr;
	i2c_reg = BAS_DSP_SUB_STATUS;
	i2c_size = BAS_DSP_REG_SIZE_STATE;
	i2c_rbuffer(d->bus);
	if (buffer_byte > 0x00)
		errorcount(BAS_ERRORCOUNT_SW_DSP);
}
