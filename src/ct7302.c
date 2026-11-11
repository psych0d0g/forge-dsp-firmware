/*
 * Port of libs/spdif/ct7302 (functions.bas, 20/, 22/).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "flavour.h"
#if BAS_MODUL_CT7302 > 0
#include "ct7302.h"
#include "eram.h"
#include "i2c.h"
#include "registers.h"
#include "store.h"

#if BAS_MODUL_CT7302_I2C == 1
#define BUS 1
#else
#define BUS 2
#endif

uint8_t ct7302_active_channel;

static void w(uint8_t reg, uint8_t v) { i2c_reg = reg; buffer_byte = v; i2c_wbyte(BUS); }
static uint8_t r(uint8_t reg) { i2c_reg = reg; i2c_rbyte(BUS); return buffer_byte; }
static void rmw(uint8_t reg, uint8_t and_mask, uint8_t or_mask)
{
	r(reg);
	buffer_byte = (uint8_t)((buffer_byte & and_mask) | or_mask);
	i2c_wbyte(BUS);  /* i2c_reg still set */
}

/* i2c_device_addr is set by the caller, as in BASCOM */
static void ct7302_init_sub(void)
{
	rmw(BAS_CT7302_REG_SRC_RATIO_FREQ, 0x80, 0x13);
	w(BAS_CT7302_REG_RXTX_CONFIG2, 0x02);           /* right volume independent */
	rmw(BAS_CT7302_REG_POWER_CONFIG, 0xFC, 0);       /* osc/xtal normal */
	rmw(BAS_CT7302_REG_TIMING_JITTER_CONFIG, 0x1F, 0);
	rmw(BAS_CT7302_REG_CONFIG1, 0xC0, 0);            /* fading normal */
	w(BAS_CT7302_REG_CDR_FREQ_COARSE, 0x23);
	w(BAS_CT7302_REG_CDR_PHASE_CS_GAIN, 0x19);
	w(BAS_CT7302_REG_CDR_PHASE_VS_GAIN, 0x1E);
	rmw(BAS_CT7302_REG_IN_PLL_CONFIG, 0xF3, 0);
	rmw(BAS_CT7302_REG_OUT_PLL_CONFIG2, 0xF3, 0);
	rmw(BAS_CT7302_REG_MISCELLANEOUS1, 0xCF, 0x02);
	w(BAS_CT7302_REG_INOUT_PLL_LOW_BAND, 0x33);
	w(BAS_CT7302_REG_SEL_DIGITAL_OSC_FREQ, 0x08);
	w(BAS_CT7302_REG_JITTER_RXTX_OSC, 0x01);
	w(BAS_CT7302_REG_PIN_DSD_IN, BAS_CT7302_REG_PIN_DSD_IN_INIT);
	w(BAS_CT7302_REG_INPUT_SRCMODE, BAS_CT7302_REG_INPUT_SRCMODE_INIT);
	w(BAS_CT7302_REG_RXTX_CONFIG1, BAS_CT7302_REG_RXTX_CONFIG1_INIT);
	w(BAS_CT7302_REG_SILENCE_DETECT, BAS_CT7302_REG_SILENCE_DETECT_INIT);
	uint8_t opt = eram_rb(ERAM_CT7302_REGISTER_OPTIONS);
	/* bit 0 ("192k for coaxial/optical spdif") has no code in the original */
	if (opt & 0x80)
		rmw(BAS_CT7302_REG_RX_STATE1, 0xFF, 0x40);   /* SPDIF RX 32 bit */
}

static uint8_t detect(uint8_t adr, uint8_t rdy)
{
	if (!(channel_open & (1u << (4 + BUS))))
		return 0;
	twi_start(BUS);
	twi_wbyte(BUS, adr);
	if (i2c_err == 0)
		hardware_other |= 1u << rdy;
	else
		errorcount((uint8_t)(rdy + BAS_ERRORCOUNT_OFFSET_OTHER));
	twi_stop(BUS);
	return 1;
}

void spdif_init(void)
{
#if BAS_MODUL_CT7302_20 == 1
	detect(0x20, BAS_CT7302_20_RDY);
	if (hardware_other & (1u << BAS_CT7302_20_RDY)) {
		i2c_device_addr = 0x20;
		ct7302_init_sub();
		ct7302_active_channel = 0x00;
	}
#endif
#if BAS_MODUL_CT7302_22 == 1
	detect(0x22, BAS_CT7302_22_RDY);
	if (hardware_other & (1u << BAS_CT7302_22_RDY)) {
		i2c_device_addr = 0x22;
		ct7302_init_sub();
	}
#endif
}

void ct7302_20_set_mux(uint8_t channel)
{
	if (!(hardware_other & (1u << BAS_CT7302_20_RDY)))
		return;
#if BAS_MODUL_CT7302_USB_AT_I2S == 1
	if (channel == 0x01)
		channel = 0x05;  /* USB (XMOS) comes in over I2S at RX5 */
#endif
	ct7302_active_channel = channel;
	i2c_device_addr = 0x20;
	rmw(BAS_CT7302_REG_INPUT_SRCMODE, 0xF8, channel);
}

void ct7302_20_set_vol(uint8_t channel, uint8_t value)
{
	if (!(hardware_other & (1u << BAS_CT7302_20_RDY)))
		return;
	i2c_device_addr = 0x20;
	w(channel == 1 ? BAS_CT7302_REG_VOL_RIGHT : BAS_CT7302_REG_VOL_LEFT, (uint8_t)(255 - value));
}

uint8_t ct7302_20_get_mux(void)
{
	i2c_device_addr = 0x20;
	uint8_t v = r(BAS_CT7302_REG_INPUT_SRCMODE) & 0x07;
#if BAS_MODUL_CT7302_USB_AT_I2S == 1
	if (v == 0x05)
		v = 0x01;
#endif
	return v;
}
#endif
