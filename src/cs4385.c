/*
 * Port of libs/da/cs4385 (V30-96LN, bus 2, 0x30).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
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
	for (uint8_t ch = 1; ch <= BAS_CS4385_MAX_CHANNEL; ch++) {
		i2c_reg = BAS_LOOKUP_B(data_cs4385_vol_reg, ch);
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

void dac_init(void)
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
#endif
