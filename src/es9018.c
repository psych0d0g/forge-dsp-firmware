/*
 * Port of libs/da/es9018 (bus 2, 0x90).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "flavour.h"
#if BAS_MODUL_ES9018 > 0
#include "bas_data.h"
#include "dac.h"
#include "flavour_data.h"
#include "i2c.h"
#include "registers.h"
#include "store.h"

#define ADR 0x90
#define BUS 2
#define RDY (hardware_other & (1u << BAS_ES9018_RDY))

static void set_mode_ctrl(uint8_t reg, uint8_t value)
{
	if (!RDY)
		return;
	i2c_device_addr = ADR;
	i2c_reg = reg;
	buffer_byte = value;
	i2c_wbyte(BUS);
}

void es9018_set_mute(uint8_t mask)
{
	i2c_device_addr = ADR;
	for (uint8_t ch = 1; ch <= BAS_ES9018_MAX_CHANNEL; ch++) {
		i2c_reg = BAS_LOOKUP_B(data_es9018_vol_reg, ch);
		if (mask & (1u << (ch - 1))) {
			buffer_byte = 0xFF;
			i2c_wbyte(BUS);
			delay_ms(BAS_DAC_MUTE_RAMP_DOWN);
		} else {
			buffer_byte = 0x00;
			i2c_wbyte(BUS);
			delay_ms(BAS_DAC_MUTE_RAMP_UP);
		}
	}
}

/* es9018_set_phase with es9018_rotate_to_byte: bit pairs of the value in reverse order */
void es9018_set_phase(uint8_t v)
{
	if (!RDY)
		return;
	i2c_device_addr = ADR;
	i2c_reg = BAS_ES9018_REG_DACA_PHASE;
	uint8_t b = 0;
	b |= (uint8_t)(((v >> 0) & 1) << 6) | (uint8_t)(((v >> 1) & 1) << 7);
	b |= (uint8_t)(((v >> 2) & 1) << 4) | (uint8_t)(((v >> 3) & 1) << 5);
	b |= (uint8_t)(((v >> 4) & 1) << 2) | (uint8_t)(((v >> 5) & 1) << 3);
	b |= (uint8_t)(((v >> 6) & 1) << 0) | (uint8_t)(((v >> 7) & 1) << 1);
	buffer_byte = b;
	i2c_wbyte(BUS);
}

static void es9018_init(void)
{
	set_mode_ctrl(BAS_ES9018_REG_AMUTE_CONFIG, BAS_ES9018_AMUTE_CONFIG_INIT);
	set_mode_ctrl(BAS_ES9018_REG_MODE_CTRL_1, BAS_ES9018_MODE_CTRL1_INIT);
	set_mode_ctrl(BAS_ES9018_REG_IIR_FIR, BAS_ES9018_REG_IIR_FIR_INIT);
	set_mode_ctrl(BAS_ES9018_REG_MODE_CTRL_5, BAS_ES9018_MODE_CTRL5_INIT);
	if (RDY) {
		i2c_device_addr = ADR;
		i2c_reg = BAS_ES9018_REG_STATUS;
		i2c_rbyte(BUS);
	}
	es9018_set_mute(BAS_ES9018_MUTE_ALL_ON);  /* es9018_set_mute_on */
}

void dac_set_mute(uint8_t mask) { es9018_set_mute(mask); }
void dac_set_phase(uint8_t v) { es9018_set_phase(v); }

void dac_init(void)
{
	if (channel_open & (1u << 6)) {
		uint8_t tries = 0;
		while (!RDY) {
			twi_start(BUS);
			twi_wbyte(BUS, ADR);
			if (i2c_err == 0)
				hardware_other |= 1u << BAS_ES9018_RDY;
			twi_stop(BUS);
			delay_ms(BAS_ES9018_LOCK_WAIT);
			if (tries > BAS_ES9018_LOCK_COUNT) {
				errorcount((uint8_t)(BAS_ES9018_RDY + BAS_ERRORCOUNT_OFFSET_OTHER));
				break;
			}
			tries++;
		}
	}
	es9018_init();
}
#endif
