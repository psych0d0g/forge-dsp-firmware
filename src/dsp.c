/*
 * Port of the register writes of libs/dsp/tas3108 (functions.bas, cores/N/functions.bas)
 * and i2cN_wbuffer_verify (libs/interface/V40/5,6).
 * PORT: the verified writes retry at most 8 times; the original repeats until the read-back
 * matches, which hangs until the watchdog if a DSP does not answer correctly.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include <string.h>
#include "bas_data.h"
#include "dsp.h"
#include "flavour.h"
#include "flavour_data.h"
#include "i2c.h"
#include "registers.h"
#include "tas3108.h"

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

void dsp_calc_core(void) { dsp_core = BAS_LOOKUP_B(data_flow_dsp_bq_core, dsp_bq_virt_nr); }
void dsp_calc_reg(void) { dsp_reg = BAS_LOOKUP_B(data_flow_dsp_bq_reg, dsp_bq_virt_nr); }

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

/* write buffer_bytes(1..size) and verify, as dsp_N_set_vol / dsp_N_set_biquad */
static void verified_write(const struct core *c, uint8_t size)
{
	uint8_t copy[BAS_DSP_REG_SIZE_BIQUAD + 1];
	memcpy(copy, buffer_bytes, (size_t)size + 1);  /* iface_verify_copy2verify */
	for (uint8_t attempt = 0; attempt < 8; attempt++) {
		uint16_t ec = iface_verify_error_count;
		i2c_device_addr = c->adr;
		i2c_reg = dsp_reg;
		i2c_size = size;
		i2c_wbuffer(c->bus);
		delay_ms(BAS_DSP_WRITEDELAY);
#if BAS_FEATURE_DATA_VERIFY > 0
		wbuffer_verify(c, size, copy);
		if (ec == iface_verify_error_count)
			return;
		i2c_reset(c->bus);
		memcpy(buffer_bytes, copy, (size_t)size + 1);  /* iface_verify_copy2buffer */
#else
		(void)ec;
		return;
#endif
	}
}

static void plain_write(const struct core *c, uint8_t reg, uint8_t size, uint8_t wait)
{
	i2c_device_addr = c->adr;
	i2c_reg = reg;
	i2c_size = size;
	i2c_wbuffer(c->bus);
	if (wait)
		delay_ms(BAS_DSP_WRITEDELAY);
}

void dsp_set_biquad(const uint32_t raw[6])
{
	const struct core *c = cur();
	if (!c)
		return;
	for (uint8_t i = 1; i <= 5; i++)
		put32((uint8_t)((i - 1) * 4 + 1), raw[i]);  /* buffer_dwords(i) */
	verified_write(c, BAS_DSP_REG_SIZE_BIQUAD);
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
