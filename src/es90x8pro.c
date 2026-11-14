/*
 * Port of libs/da/es90x8pro (bus 2, 0x90).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "boottime.h"
#include "bqramp.h"
#include "flavour.h"
#if BAS_MODUL_ES90X8PRO > 0
#include "bas_data.h"
#include "dac.h"
#include "eram.h"
#include "flavour_data.h"
#include "i2c.h"
#include "registers.h"
#include "store.h"

#define ADR 0x90
#define BUS 2
#define reg_system              0x00
#define reg_input_mux           0x01
#define reg_sdata_automute      0x02
#define reg_filterband_sysmute  0x07
#define reg_ch_map_21           0x26
#define reg_ch_map_43           0x27
#define reg_ch_map_65           0x28
#define reg_ch_map_87           0x29
#define reg_chipid_state        0x40

#define RDY (hardware_other & (1u << BAS_ES90X8PRO_RDY))

static void set_mode_ctrl(uint8_t reg, uint8_t value)
{
	if (!RDY)
		return;
	i2c_device_addr = ADR;
	i2c_reg = reg;
	buffer_byte = value;
	i2c_wbyte(BUS);
}

void es90x8pro_set_mute(uint8_t mask)
{
	/* as BASCOM: no ready check here */
	i2c_device_addr = ADR;
	/* PORT (#50): with clickless on all channels ramp at the same time (one wait instead of one
	 * per channel: 0.23 s instead of 1.8 s blocking the main loop); otherwise as the original */
	uint8_t parallel = bqramp_enabled(), down = 0, up = 0;
	for (uint8_t ch = 1; ch <= 8; ch++) {
		i2c_reg = BAS_LOOKUP_B(data_es90x8pro_vol_reg, ch);
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

static void es90x8pro_init(void)
{
	if (registers[BAS_REG_STARTUP_STATE] == BAS_STARTUP_STATE_SYSINIT)
		eram_wb(ERAM_ES90X8PRO_FB_SYSMUTE_ERAM, BAS_ES90X8PRO_FB_SYSMUTE_INIT);
	i2c_device_addr = ADR;
	i2c_reg = reg_filterband_sysmute;
	i2c_rbyte(BUS);
	/* BASCOM computes (read & 0x19) & (eram & 0xE6), always 0, and then writes cvt_bytes(1),
	 * i.e. eram & 0xE6; the read value is not used */
	uint8_t cvt = eram_rb(ERAM_ES90X8PRO_FB_SYSMUTE_ERAM) & 0xE6;
	buffer_byte = (uint8_t)(buffer_byte & 0x19 & cvt);
	set_mode_ctrl(reg_filterband_sysmute, cvt);
	set_mode_ctrl(reg_ch_map_21, BAS_ES90X8PRO_CH_MAP_21_INIT);
	set_mode_ctrl(reg_ch_map_43, BAS_ES90X8PRO_CH_MAP_43_INIT);
	set_mode_ctrl(reg_ch_map_65, BAS_ES90X8PRO_CH_MAP_65_INIT);
	set_mode_ctrl(reg_ch_map_87, BAS_ES90X8PRO_CH_MAP_87_INIT);
	set_mode_ctrl(reg_system, BAS_ES90X8PRO_SYSTEM_INIT);
	set_mode_ctrl(reg_input_mux, BAS_ES90X8PRO_INPUT_MUX_INIT);
	set_mode_ctrl(reg_sdata_automute, BAS_ES90X8PRO_SDATA_AUTOMUTE_INIT);
	if (RDY) {
		i2c_device_addr = ADR;
		i2c_reg = reg_chipid_state;
		i2c_rbyte(BUS);
	}
	es90x8pro_set_mute(BAS_ES90X8PRO_MUTE_ALL_ON);
}

void dac_set_mute(uint8_t mask) { es90x8pro_set_mute(mask); }
void dac_set_phase(uint8_t v) { (void)v; }  /* 96ELN-3: phase is in the DSP */

static void dac_init_body(void)
{
	if (channel_open & (1u << 6)) {
		uint8_t tries = 0;
		while (!RDY) {
			twi_start(BUS);
			twi_wbyte(BUS, ADR);
			if (i2c_err == 0)
				hardware_other |= 1u << BAS_ES90X8PRO_RDY;
			twi_stop(BUS);
			delay_ms(BAS_ES90X8PRO_LOCK_WAIT);
			if (tries > BAS_ES90X8PRO_LOCK_COUNT) {
				errorcount((uint8_t)(BAS_ES90X8PRO_RDY + BAS_ERRORCOUNT_OFFSET_OTHER));
				break;
			}
			tries++;
		}
	}
	es90x8pro_init();
}

void dac_init(void)
{
	dac_init_body();
	boot_mark(BT_DAC);
}
#endif
