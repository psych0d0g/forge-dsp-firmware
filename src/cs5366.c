/*
 * Port of libs/ad/cs536x/6channel (V30-96LN, bus 1, 0x98).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "flavour.h"
#if BAS_MODUL_CS5366 > 0
#include "adc.h"
#include "i2c.h"
#include "registers.h"
#include "store.h"

#define ADR 0x98
#define BUS 1
#define RDY (hardware_other & (1u << BAS_CS5366_RDY))

static void wr(uint8_t reg, uint8_t v)
{
	i2c_device_addr = ADR;
	i2c_reg = reg;
	buffer_byte = v;
	i2c_wbyte(BUS);
}

/* cs5366_rotate (modul_cs5366_rev_pairs): swap bit pairs 1:0 and 3:2 */
static uint8_t rotate(uint8_t v)
{
	uint8_t b = v;
	b = (uint8_t)((b & ~0x0F) | ((v & 0x03) << 2) | ((v >> 2) & 0x03));
	return b;
}

void cs5366_set_mute(uint8_t v)
{
	if (!RDY)
		return;
#if BAS_MODUL_CS5366_REV_PAIRS == 1
	wr(BAS_CS5366_REG_MUTE, rotate(v));
#else
	wr(BAS_CS5366_REG_MUTE, v);
#endif
}

void cs5366_set_highpass(uint8_t v)
{
#if BAS_MODUL_CS5366_REV_PAIRS == 1
	wr(BAS_CS5366_REG_HIGHPASS_FILTER, rotate(v));
#else
	wr(BAS_CS5366_REG_HIGHPASS_FILTER, v);
#endif
}

void adc_init(void)
{
	if (channel_open & (1u << 5)) {
		twi_start(BUS);
		twi_wbyte(BUS, ADR);
		if (i2c_err == 0)
			hardware_other |= 1u << BAS_CS5366_RDY;
		else
			errorcount((uint8_t)(BAS_CS5366_RDY + BAS_ERRORCOUNT_OFFSET_OTHER));
		twi_stop(BUS);
	}
	if (!RDY)
		return;
	buffer_byte = 0;
	i2c_device_addr = ADR;
	i2c_reg = BAS_CS5366_REG_ID_REVISION;
	i2c_rbyte(BUS);
	wr(BAS_CS5366_REG_MODE_CTRL, BAS_CS5366_MODE_CTRL_INIT);
	wr(BAS_CS5366_REG_POWERDOWN_MASK, 0x18);
	wr(BAS_CS5366_REG_OVERFLOW_MASK, 0x3F);
	wr(BAS_CS5366_REG_SDOUT_CTRL, 0x08);
	i2c_device_addr = ADR;               /* cs5366_reset_overflow: read clears the state */
	i2c_reg = BAS_CS5366_REG_OVERFLOW_STATE;
	i2c_rbyte(BUS);
}
#endif
