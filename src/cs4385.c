/*
 * Port of libs/da/cs4385 (V30-96LN, bus 2, 0x30).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "boottime.h"
#include "bqramp.h"
#include "flavour.h"
#if BAS_MODUL_CS4385 > 0
#include "bas_data.h"
#include "dac.h"
#include "flavour_data.h"
#include "i2c.h"
#include "registers.h"
#include "store.h"

#define ADR 0x30
#define BUS 2
#define RDY (hardware_other & (1u << BAS_CS4385_RDY))

uint8_t cs4385_reg, cs4385_value;

void cs4385_set_reg(void)
{
	if (!RDY)
		return;
	i2c_device_addr = ADR;
	i2c_reg = cs4385_reg;
	buffer_byte = cs4385_value;
	i2c_wbyte(BUS);
}

void cs4385_set_mute(uint8_t mask)
{
	if (!RDY)
		return;
	i2c_device_addr = ADR;
	/* PORT (#50): with clickless on all channels ramp at the same time (one wait instead of one
	 * per channel: 0.23 s instead of 1.8 s blocking the main loop); otherwise as the original */
	uint8_t parallel = bqramp_enabled(), down = 0, up = 0;
	for (uint8_t ch = 1; ch <= BAS_CS4385_MAX_CHANNEL; ch++) {
		i2c_reg = BAS_LOOKUP_B(data_cs4385_vol_reg, ch);
		if (mask & (1u << (ch - 1))) {
			buffer_byte = 0xFF;
			i2c_wbyte(BUS);
			if (parallel) down = 1; else delay_ms(BAS_DAC_MUTE_RAMP_DOWN);
		} else {
			buffer_byte = 0x00;
			i2c_wbyte(BUS);
			if (parallel) up = 1; else delay_ms(BAS_DAC_MUTE_RAMP_UP);
		}
	}
	if (parallel)
		delay_ms(up ? BAS_DAC_MUTE_RAMP_UP : down ? BAS_DAC_MUTE_RAMP_DOWN : 0);
}

void cs4385_set_phase(uint8_t v)
{
	if (!RDY)
		return;
	i2c_device_addr = ADR;
	i2c_reg = BAS_CS4385_REG_PHASE;
#if BAS_MODUL_CS4385_REV_PAIRS == 1
	uint8_t b = 0;  /* cs4385_rotate_to_byte */
	b |= (uint8_t)(((v >> 0) & 1) << 6) | (uint8_t)(((v >> 1) & 1) << 7);
	b |= (uint8_t)(((v >> 2) & 1) << 4) | (uint8_t)(((v >> 3) & 1) << 5);
	b |= (uint8_t)(((v >> 4) & 1) << 2) | (uint8_t)(((v >> 5) & 1) << 3);
	b |= (uint8_t)(((v >> 6) & 1) << 0) | (uint8_t)(((v >> 7) & 1) << 1);
	buffer_byte = b;
#else
	buffer_byte = v;
#endif
	i2c_wbyte(BUS);
}

void dac_set_mute(uint8_t mask) { cs4385_set_mute(mask); }
void dac_set_phase(uint8_t v) { cs4385_set_phase(v); }

static void dac_init_body(void)
{
	if (channel_open & (1u << 6)) {
		twi_start(BUS);
		twi_wbyte(BUS, ADR);
		if (i2c_err == 0)
			hardware_other |= 1u << BAS_CS4385_RDY;
		else
			errorcount((uint8_t)(BAS_CS4385_RDY + BAS_ERRORCOUNT_OFFSET_OTHER));
		twi_stop(BUS);
	}
	cs4385_reg = BAS_CS4385_REG_MODE_CTRL;  cs4385_value = BAS_CS4385_MODE_CTRL_INIT;  cs4385_set_reg();
	cs4385_reg = BAS_CS4385_REG_MODE_CTRL;  cs4385_value = BAS_CS4385_MODE_CTRL_START; cs4385_set_reg();
	cs4385_reg = BAS_CS4385_REG_RAMP_MUTE;  cs4385_value = BAS_CS4385_RAMP_MUTE_INIT;  cs4385_set_reg();
	cs4385_reg = BAS_CS4385_REG_PCM_CTRL;   cs4385_value = BAS_CS4385_PCM_CTRL_INIT;   cs4385_set_reg();
	cs4385_set_mute(BAS_CS4385_MUTE_ALL_ON);
	if (RDY) {
		buffer_byte = 0;
		i2c_device_addr = ADR;
		i2c_reg = BAS_CS4385_REG_ID_REVISION;
		i2c_rbyte(BUS);  /* id/revision, only for the debug output in BASCOM */
	}
}

void dac_init(void)
{
	dac_init_body();
	boot_mark(BT_DAC);
}
#endif
