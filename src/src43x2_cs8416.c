/*
 * Port of libs/spdif/src43x2 (bus 1, 0xE0) and libs/spdif/cs8416 (bus 2, 0x20), V30-96LN.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "boottime.h"
#include "flavour.h"
#if BAS_MODUL_SRC43X2 > 0 || BAS_MODUL_CS8416 > 0
#include "ct7302.h"
#include "i2c.h"
#include "registers.h"
#include "store.h"

static uint8_t detect(uint8_t bus, uint8_t adr, uint8_t rdy)
{
	if (channel_open & (1u << (4 + bus))) {
		twi_start(bus);
		twi_wbyte(bus, adr);
		if (i2c_err == 0)
			hardware_other |= 1u << rdy;
		else
			errorcount((uint8_t)(rdy + BAS_ERRORCOUNT_OFFSET_OTHER));
		twi_stop(bus);
	}
	return (hardware_other >> rdy) & 1;
}

static void wr(uint8_t bus, uint8_t adr, uint8_t reg, uint8_t v)
{
	i2c_device_addr = adr;
	i2c_reg = reg;
	buffer_byte = v;
	i2c_wbyte(bus);
}

#if BAS_MODUL_SRC43X2 > 0
static void src43x2_init_register(void)
{
	wr(1, 0xE0, BAS_SRC43X2_REG_PWR_CTRL_RESET, BAS_SRC43X2_REG_PWR_CTRL_RESET_INIT);
	wr(1, 0xE0, BAS_SRC43X2_REG_PORTA_CTRL_1, BAS_SRC43X2_REG_PORTA_CTRL_1_INIT);
	/* receiver: 5 registers with auto increment, sent backwards from buffer_bytes(5) */
	i2c_device_addr = 0xE0;
	i2c_reg = BAS_SRC43X2_REG_AUTOINC + BAS_SRC43X2_REG_RECEIVER_CTRL_1;
	i2c_size = 5;
	buffer_bytes[5] = BAS_SRC43X2_REG_RECEIVER_CTRL_1_INIT;
	buffer_bytes[4] = 0x00;
#if BAS_FLOW_MCLK_KHZ == 12288
	buffer_bytes[3] = 0x12;  /* P=1, J=8 */
#elif BAS_FLOW_MCLK_KHZ == 24576
	buffer_bytes[3] = 0x22;  /* P=2, J=8 */
#endif
	buffer_bytes[2] = 0x00;
	buffer_bytes[1] = 0x00;
	i2c_wbuffer(1);
	/* source: src_in = DIR, direct down-sampling + AUTODEM, attenuation 0 */
	i2c_device_addr = 0xE0;
	i2c_reg = BAS_SRC43X2_REG_AUTOINC + BAS_SRC43X2_REG_SRC_CTRL_1;
	i2c_size = 5;
	buffer_bytes[5] = 0x02;
	buffer_bytes[4] = 0x24;
	buffer_bytes[3] = 0x00;
	buffer_bytes[2] = 0x00;
	buffer_bytes[1] = 0x00;
	i2c_wbuffer(1);
}
#endif

#if BAS_MODUL_SRC43X2 > 0
void src43x2_set_mux(uint8_t channel)
{
	wr(1, 0xE0, BAS_SRC43X2_REG_RECEIVER_CTRL_1, (uint8_t)(channel | BAS_SRC43X2_REG_RECEIVER_CTRL_1_INIT));
}

void src43x2_set_vol(uint8_t channel, uint8_t value)
{
	if (!(hardware_other & (1u << BAS_SRC43X2_RDY)))
		return;
	/* src43x2 max attenuation at 0xFF */
	wr(1, 0xE0, channel == 1 ? BAS_SRC43X2_REG_SRC_CTRL_AR : BAS_SRC43X2_REG_SRC_CTRL_AL, (uint8_t)(255 - value));
}
#endif

#if BAS_MODUL_CS8416 > 0
static void cs8416_init(void)
{
	if (!(hardware_other & (1u << BAS_CS8416_RDY)))
		return;
	buffer_byte = 0;
	i2c_device_addr = 0x20;
	i2c_reg = BAS_CS8416_REG_ID_VERSION;
	i2c_rbyte(2);
	wr(2, 0x20, BAS_CS8416_REG_CTRL1, BAS_CS8416_REG_CTRL1_INIT);
	wr(2, 0x20, BAS_CS8416_REG_CTRL3, BAS_CS8416_REG_CTRL3_INIT);
	wr(2, 0x20, BAS_CS8416_REG_CTRL4, BAS_CS8416_REG_CTRL4_INIT);
	wr(2, 0x20, BAS_CS8416_REG_REC_MASK, BAS_CS8416_REG_REC_MASK_INIT);
	wr(2, 0x20, BAS_CS8416_REG_INT_MASK, BAS_CS8416_REG_INT_MASK_INIT);
}
#endif

#if BAS_MODUL_CS8416 > 0
static uint8_t cs8416_rd(uint8_t reg)
{
	if (!(hardware_other & (1u << BAS_CS8416_RDY)))
		return buffer_byte;  /* as BASCOM: buffer_byte unchanged */
	i2c_device_addr = BAS_CS8416_ADR_W;
	i2c_reg = reg;
	i2c_rbyte(2);
	return buffer_byte;
}

uint8_t cs8416_get_rec_error(void) { return cs8416_rd(BAS_CS8416_REG_REC_ERROR); }
uint8_t cs8416_get_form_state(void) { return cs8416_rd(BAS_CS8416_REG_FORM_STATE); }

void cs8416_set_mux(uint8_t v)
{
	if (!(hardware_other & (1u << BAS_CS8416_RDY)))
		return;
	wr(2, BAS_CS8416_ADR_W, BAS_CS8416_REG_CTRL4, (uint8_t)((v << 3) + BAS_CS8416_REG_CTRL4_INIT));
}
#endif

static void spdif_init_body(void)
{
#if BAS_MODUL_SRC43X2 > 0
	if (detect(1, 0xE0, BAS_SRC43X2_RDY))
		src43x2_init_register();
#endif
#if BAS_MODUL_CS8416 > 0
	detect(2, 0x20, BAS_CS8416_RDY);
	cs8416_init();
#endif
}

void spdif_init(void)
{
	spdif_init_body();
	boot_mark(BT_SPDIF);
}
#endif
