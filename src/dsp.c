/*
 * Port of the register writes of libs/dsp/tas3108 (functions.bas, cores/N/functions.bas)
 * and i2cN_wbuffer_verify (libs/interface/V40/5,6).
 * PORT: the verified writes retry at most 8 times; the original repeats until the read-back
 * matches, which hangs until the watchdog if a DSP does not answer correctly.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include <string.h>
#include "bas_data.h"
#include "cmd.h"
#include "dsp.h"
#include "flavour.h"
#include "flavour_data.h"
#include "i2c.h"
#include "registers.h"
#include "tas3108.h"
#include "dsp_pool.h"

uint8_t dsp_core, dsp_reg;
uint16_t dsp_bq_virt_nr;
uint16_t iface_verify_error_count;

struct core { uint8_t bus, adr, rdy; };
static const struct core cores[4] = {
	{ 0, 0, 0 },
	{ 1, 0x68, BAS_TAS3108_1_RDY },
#if BAS_MODUL_TAS3108_CORES > 1
	{ 2, 0x68, BAS_TAS3108_2_RDY },
#else
	{ 0, 0, 0 },
#endif
#if BAS_MODUL_TAS3108_CORES > 2
	{ 2, 0x6A, BAS_TAS3108_3_RDY },
#else
	{ 0, 0, 0 },
#endif
};

static const struct core *cur(void)
{
	if (dsp_core < 1 || dsp_core > BAS_MODUL_TAS3108_CORES)
		return NULL;
	const struct core *c = &cores[dsp_core];
	return (hardware_dsp & (1u << c->rdy)) ? c : NULL;
}

static void put32(uint8_t at, uint32_t v)
{
	for (uint8_t i = 0; i < 4; i++)
		buffer_bytes[at + i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get32(uint8_t at)
{
	uint32_t v = 0;
	for (uint8_t i = 0; i < 4; i++)
		v |= (uint32_t)buffer_bytes[at + i] << (8 * i);
	return v;
}

void dsp_calc_core(void) { dsp_core = flow_bq_core(dsp_bq_virt_nr); }  /* PORT (#47): with the spares */
void dsp_calc_reg(void) { dsp_reg = flow_bq_reg(dsp_bq_virt_nr); }

/* PORT (#66): wait after a write into the DSP as a build option (the original: dsp_writedelay = 16 ms);
 * every write is read back and repeated on a difference anyway */
#ifdef FORGE_DSP_WRITEDELAY
#define DSP_WRITEDELAY FORGE_DSP_WRITEDELAY
#else
#define DSP_WRITEDELAY BAS_DSP_WRITEDELAY
#endif

/* i2cN_wbuffer_verify: read back (twice at most), count differences */
static void wbuffer_verify(const struct core *c, uint8_t size, const uint8_t *expect)
{
	uint8_t e_count = 0;
	for (uint8_t i = 1; i <= 2; i++) {
		i2c_device_addr = c->adr;
		i2c_reg = dsp_reg;
		i2c_size = size;
		i2c_rbuffer(c->bus);
		e_count = 0;
		for (uint8_t ii = 1; ii <= size; ii++)
			if (expect[ii] != buffer_bytes[ii])
				e_count++;
		if (e_count == 0)
			break;
		delay_ms(5);
	}
	if (e_count > 0) {
		errorcount(BAS_ERRORCOUNT_FW_IFACE_VERIFY);
		iface_verify_error_count++;
	}
}

/* write buffer_bytes(1..size) and verify, as dsp_N_set_vol / dsp_N_set_biquad; 1 = verified */
static uint8_t verified_write(const struct core *c, uint8_t size)
{
	uint8_t copy[BAS_DSP_REG_SIZE_BIQUAD + 1];
	memcpy(copy, buffer_bytes, (size_t)size + 1);  /* iface_verify_copy2verify */
	for (uint8_t attempt = 0; attempt < 8; attempt++) {
		uint16_t ec = iface_verify_error_count;
		i2c_device_addr = c->adr;
		i2c_reg = dsp_reg;
		i2c_size = size;
		i2c_wbuffer(c->bus);
		delay_ms(DSP_WRITEDELAY);
#if BAS_FEATURE_DATA_VERIFY > 0
		wbuffer_verify(c, size, copy);
		if (ec == iface_verify_error_count)
			return 1;
		i2c_reset(c->bus);
		memcpy(buffer_bytes, copy, (size_t)size + 1);  /* iface_verify_copy2buffer */
#else
		(void)ec;
		return 1;
#endif
	}
	return 0;
}

static void plain_write(const struct core *c, uint8_t reg, uint8_t size, uint8_t wait)
{
	i2c_device_addr = c->adr;
	i2c_reg = reg;
	i2c_size = size;
	i2c_wbuffer(c->bus);
	if (wait)
		delay_ms(DSP_WRITEDELAY);
}

/* PORT (#58): buffer_bytes(1..size) to dsp_core/dsp_reg, verified as the biquads */
void dsp_set_reg(uint8_t size)
{
	const struct core *c = cur();
	if (c && size <= BAS_DSP_REG_SIZE_BIQUAD)
		verified_write(c, size);
}

/* PORT (#66): what each biquad place of the DSPs holds, as CRC-16 of its coefficients (0 = not known).
 * A place that already holds the coefficients is not written again: a preset change rewrites all
 * places, most of them the same (empty in both presets), and each write costs dsp_writedelay
 * (16 ms) plus the read back. Starts empty (RAM after every reset); every DSP restart is a reset
 * of the MCU. Indexed by the virtual biquad number (dsp_bq_virt_nr), which all callers set. */
#define BQ_PLACES (BAS_FLOW_MAX_DSP_BIQUADS + DSP_BQ_EXTRA_MAX + 1)  /* the pools' spares included */
static uint16_t bq_held[BQ_PLACES];
uint16_t dsp_bq_skipped, dsp_bq_written;  /* since the start (diagnostics, tests) */
void dsp_bq_forget(void) { memset(bq_held, 0, sizeof bq_held); }

void dsp_set_biquad(const uint32_t raw[6])
{
	const struct core *c = cur();
	if (!c)
		return;
	for (uint8_t i = 1; i <= 5; i++)
		put32((uint8_t)((i - 1) * 4 + 1), raw[i]);  /* buffer_dwords(i) */
	uint16_t crc = (uint16_t)(bas_crc16(&buffer_bytes[1], BAS_DSP_REG_SIZE_BIQUAD) | 1u);  /* never 0 */
	uint16_t n = dsp_bq_virt_nr;
	if (n < BQ_PLACES && bq_held[n] == crc) {
		dsp_bq_skipped++;
		return;
	}
	dsp_bq_written++;
	uint8_t ok = verified_write(c, BAS_DSP_REG_SIZE_BIQUAD);
	if (n < BQ_PLACES)
		bq_held[n] = ok ? crc : 0;
}

void dsp_get_biquad(uint32_t raw[6])
{
	const struct core *c = cur();
	if (!c)
		return;
	i2c_device_addr = c->adr;
	i2c_reg = dsp_reg;
	i2c_size = BAS_DSP_REG_SIZE_BIQUAD;
	i2c_rbuffer(c->bus);
	for (uint8_t i = 1; i <= 5; i++)
		raw[i] = get32((uint8_t)((i - 1) * 4 + 1));
}

void dsp_set_vol_out(int32_t vol_calc)
{
	const struct core *c = cur();
	if (!c)
		return;
	put32(1, (uint32_t)vol_calc);  /* buffer_long */
	verified_write(c, BAS_DSP_REG_SIZE_VOL);
}

void dsp_set_mux(uint32_t state)
{
	const struct core *c = cur();
	if (!c)
		return;
	put32(1, state);
	plain_write(c, dsp_reg, BAS_DSP_REG_SIZE_MUX, 0);
}

uint32_t dsp_get_mux(void)
{
	const struct core *c = cur();
	if (!c)
		return 0;
	i2c_device_addr = c->adr;
	i2c_reg = dsp_reg;
	i2c_size = BAS_DSP_REG_SIZE_MUX;
	i2c_rbuffer(c->bus);
	return get32(1);
}

void dsp_set_delay(uint32_t value)
{
	const struct core *c = cur();
	if (!c)
		return;
	put32(1, value);
	plain_write(c, dsp_reg, BAS_DSP_REG_SIZE_DELAY, 1);
}

static void ack_delay(uint8_t n)
{
	const struct core *c = &cores[n];
	if (!(hardware_dsp & (1u << c->rdy)))
		return;
	put32(1, 1);
	plain_write(c, BAS_DSP_SUB_DELAY_ACK, BAS_DSP_REG_SIZE_DELAY, 1);
}

void dsp_ack_delay(void)
{
	/* every core, as dsp_ack_delay */
	for (uint8_t n = 1; n <= BAS_MODUL_TAS3108_CORES; n++)
		ack_delay(n);
}

void dsp_1_ack_delay(void) { ack_delay(1); }  /* input delays live in core 1 */

void dsp_set_phase_reg(uint32_t value)
{
	const struct core *c = cur();
	if (!c)
		return;
	put32(1, value);
	plain_write(c, dsp_reg, BAS_DSP_REG_SIZE_PHASE, 1);
}

void dsp_init_vum(void)
{
	dsp_core = 1;  /* dsp_init_vum calls dsp_1_init_vum */
	const struct core *c = cur();
	if (!c)
		return;
	plain_write(c, dsp_reg, BAS_DSP_REG_SIZE_VUM, 0);
}

void dsp_get_vum(void)
{
	dsp_core = 1;
	const struct core *c = cur();
	if (!c)
		return;
	i2c_device_addr = c->adr;
	i2c_reg = dsp_reg;
	i2c_size = BAS_DSP_REG_SIZE_VUM;
	i2c_rbuffer(c->bus);
}

/* PORT (#51): TAS3108 PEEK (SLES152B 7.4): register 0x06 = [unused, memory, addr high, addr low]
 * (normal bit order, checked on the device), then 0x07 returns 8 bytes, LSB first in
 * buffer_bytes(1). mem: 1 = coefficients, 2 = data. -> 0 if the core is not present. */
uint8_t dsp_peek(uint8_t core, uint8_t mem, uint16_t addr, uint8_t out[8])
{
	if (core < 1 || core > BAS_MODUL_TAS3108_CORES)
		return 0;
	const struct core *c = &cores[core];
	if (!(hardware_dsp & (1u << c->rdy)))
		return 0;
	buffer_bytes[4] = 0;  /* sent backwards: [4] first */
	buffer_bytes[3] = mem;
	buffer_bytes[2] = (uint8_t)(addr >> 8);
	buffer_bytes[1] = (uint8_t)addr;
	i2c_device_addr = c->adr;
	i2c_reg = 0x06;
	i2c_size = 4;
	i2c_wbuffer(c->bus);
	i2c_device_addr = c->adr;
	i2c_reg = 0x07;
	i2c_size = 8;
	i2c_rbuffer(c->bus);
	for (uint8_t i = 0; i < 8; i++)
		out[i] = buffer_bytes[i + 1];
	return 1;
}
