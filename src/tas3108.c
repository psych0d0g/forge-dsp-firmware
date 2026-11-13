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
#include "dsp_pool.h"
#include "log.h"

#define DSP_CORE_RETRIES 8     /* PORT (#45): 8 x 250 ms = 2 s more per core */
#define DSP_CORE_RETRY_MS 250
void watchdog_kick(void);

uint16_t hardware_dsp;
uint8_t dsp_state_error;  /* a core reported an error state (no program loaded) during this start */

/* PORT diagnostics (#57) */
struct dsp_boot_rec dsp_boot_now;
#ifdef __AVR__
struct dsp_boot_rec dsp_boot_prev __attribute__((section(".noinit")));
#else
struct dsp_boot_rec dsp_boot_prev;  /* the native harness "resets" within one process */
#endif

static uint8_t boot_sum(const struct dsp_boot_rec *r)
{
	const uint8_t *p = (const uint8_t *)r;
	uint8_t s = 0;
	for (uint8_t i = 2; i < sizeof *r; i++)
		s = (uint8_t)(s + p[i]);
	return s;
}

void dsp_boot_keep(void)
{
	for (uint8_t b = 0; b < 3; b++)
		dsp_boot_now.bus[b] = dspboot_diag[b];
	dsp_boot_prev = dsp_boot_now;
	dsp_boot_prev.magic = DSP_BOOT_MAGIC;
	dsp_boot_prev.check = boot_sum(&dsp_boot_prev);
}

void dsp_boot_power_on(void)
{
	dsp_boot_prev.magic = 0;
}

uint8_t dsp_boot_prev_valid(void)
{
	return dsp_boot_prev.magic == DSP_BOOT_MAGIC && dsp_boot_prev.check == boot_sum(&dsp_boot_prev);
}

struct dsp_def {
	uint8_t core, bus, eeprom_adr, core_adr, eeprom_rdy, core_rdy;
	const uint8_t *code;
	uint16_t chksum_eram;
};

/* FORGE_DSP_OPT=1 (issue #47): the programs with the delay waits filled (tools/dsp_opt_gen.py,
 * bit-identical to the original in the simulator); V30-96LN has another layout and keeps its own */
#ifndef FORGE_DSP_OPT
#define FORGE_DSP_OPT 0
#endif
#if FORGE_DSP_OPT && __has_include("dsp_opt.h")
#include "dsp_opt.h"
#define DSP_1_CODE dsp_1_code_opt
#define DSP_2_CODE dsp_2_code_opt
#else
#define DSP_1_CODE dsp_1_code
#define DSP_2_CODE dsp_2_code
#endif

static const struct dsp_def dsps[] = {
#if BAS_MODUL_TAS3108_CORES > 0
	{ 1, 1, 0xA0, 0x68, BAS_AT24C512_1_RDY, BAS_TAS3108_1_RDY, DSP_1_CODE, ERAM_DSP_1_EEPROM_CHKSUM_ERAM },
#endif
#if BAS_MODUL_TAS3108_CORES > 1
	{ 2, 2, 0xA0, 0x68, BAS_AT24C512_2_RDY, BAS_TAS3108_2_RDY, DSP_2_CODE, ERAM_DSP_2_EEPROM_CHKSUM_ERAM },
#endif
#if BAS_MODUL_TAS3108_CORES > 2
	{ 3, 2, 0xA2, 0x6A, BAS_AT24C512_3_RDY, BAS_TAS3108_3_RDY, DSP_2_CODE, ERAM_DSP_3_EEPROM_CHKSUM_ERAM },
#endif
};

/* PORT (#47): the image comes from src/dsp_pool.c, which builds the program from the pool
 * lengths (the base image unchanged with the default lengths or without pools) */
static uint16_t data_size(const struct dsp_def *d)
{
	struct dsp_img g;
	dsp_img_open(&g, d->core, d->code);
	return g.size;
}

/* PORT fix (#47): the original computes the checksum of the program only at a factory reset
 * and uses the stored value otherwise, so a firmware with another DSP program would not write it
 * to the DSP's EEPROM. The port always computes it (24 KB, a few ms) and keeps the stored value
 * up to date. */
static uint16_t data_get_chksum(const struct dsp_def *d)
{
	struct dsp_img g;
	uint16_t sum = 0;
	dsp_img_open(&g, d->core, d->code);
	for (uint16_t i = 0; i < g.size; i++)
		sum = (uint16_t)(sum + dsp_img_next(&g));
	if (eram_rw(d->chksum_eram) != sum)
		eram_ww(d->chksum_eram, sum);
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
	struct dsp_img g;
	dsp_img_open(&g, d->core, d->code);
	uint8_t page = 0;
	for (uint16_t count = 1; count <= size; count++) {
		twi_wbyte(d->bus, dsp_img_next(&g));
		page++;
		if (page > BAS_DSP_EEPROM_PAGE_SIZE) {
			eeprom_restart(d, count);  /* next byte goes to address count */
			page = 0;
		}
	}
	twi_stop(d->bus);
	delay_ms(BAS_EEPROM_PAGE_WRITE_DELAY);
}

/* PORT (#58/#57): after the long EEPROM reads and writes an EEPROM held SDA low on the DEV unit
 * (13.11.2026: bus 2 without a single edge after the reset release, core 3 in the error state).
 * The cores cannot load their program then; free the bus (8 SCL pulses) before they start. */
static void bus_release(const struct dsp_def *d, struct dsp_boot_core *r)
{
	if (twi_sda(d->bus) == 0) {
		r->bus_cleared++;
		log_event(LOG_WARN, LOGM_DSP, LOGE_DSP_BUS_CLEARED, d->bus, d->core);
	}
	i2c_reset(d->bus);  /* clears only if SDA is low */
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
		struct dsp_boot_core *r = &dsp_boot_now.core[d->core - 1];
		/* PORT (#57): read the written program back; one more try if the sum is wrong */
		for (uint8_t i = 0; i < 2; i++) {
			eeprom_clear_mem(d);
			eeprom_copy_data(d);
			r->written = (uint8_t)(i + 1);
			watchdog_kick();
			r->verify = eeprom_get_chksum(d, size) == data_chksum ? 1 : 2;
			if (r->verify == 1)
				break;
		}
		log_event(r->verify == 1 ? LOG_INFO : LOG_ERROR, LOGM_DSP, LOGE_DSP_EEPROM, d->core,
		          (uint16_t)(r->verify | r->written << 8));  /* PORT (#60) */
	}
	bus_release(d, &dsp_boot_now.core[d->core - 1]);
}

void dsp_core_init(uint8_t n)
{
	if (n < 1 || n > sizeof dsps / sizeof dsps[0])
		return;
	const struct dsp_def *d = &dsps[n - 1];
	if (channel_open & (1u << (4 + d->bus))) {
		/* PORT fix (#45): a core that is still loading its program does not answer yet; probe
		 * again every DSP_CORE_RETRY_MS up to DSP_CORE_RETRIES times before it counts as missing */
		for (uint8_t i = 0;; i++) {
			twi_start(d->bus);
			twi_wbyte(d->bus, d->core_adr);
			uint8_t ok = i2c_err == 0;
			twi_stop(d->bus);
			if (ok) {
				hardware_dsp |= 1u << d->core_rdy;
				break;
			}
			if (i >= DSP_CORE_RETRIES) {
				errorcount((uint8_t)(d->core_rdy + BAS_ERRORCOUNT_OFFSET_DSP));
				break;
			}
			dspboot_diag[d->bus].retries++;
			watchdog_kick();
			delay_ms(DSP_CORE_RETRY_MS);
		}
	}
	if (!(hardware_dsp & (1u << d->core_rdy)))
		return;
	struct dsp_boot_core *r = &dsp_boot_now.core[d->core - 1];
	r->found = 1;
	/* dsp_N_get_version, dsp_N_get_state: buffer_bytes(4..1) */
	i2c_device_addr = d->core_adr;
	i2c_reg = BAS_DSP_SUB_VERSION;
	i2c_size = BAS_DSP_REG_SIZE_VERSION;
	i2c_rbuffer(d->bus);
	for (uint8_t i = 0; i < 4; i++)
		r->version[i] = buffer_bytes[i + 1];
	i2c_device_addr = d->core_adr;
	i2c_reg = BAS_DSP_SUB_STATUS;
	i2c_size = BAS_DSP_REG_SIZE_STATE;
	i2c_rbuffer(d->bus);
	for (uint8_t i = 0; i < 4; i++)
		r->state[i] = buffer_bytes[i + 1];
	log_event(buffer_byte ? LOG_ERROR : LOG_DEBUG, LOGM_DSP, LOGE_DSP_CORE, d->core,
	          (uint16_t)(buffer_byte | 1u << 8));  /* PORT (#60) */
	if (buffer_byte > 0x00) {
		errorcount(BAS_ERRORCOUNT_SW_DSP);
		dsp_state_error = 1;  /* PORT (#47): hw_check_dsp_restart restarts once */
	}
}
