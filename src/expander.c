/*
 * Port of libs/expander/pca9554/{41,4F} and pca9555/4F (init.bas + functions.bas).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "expander.h"
#include "flavour.h"
#include "i2c.h"
#include "registers.h"
#include "store.h"

uint8_t pca9554_bits, pca9554_value;
uint16_t pca9555_bits, pca9555_value;

#define pca9554_reg_input_port  0x00
#define pca9554_reg_output_port 0x01
#define pca9554_reg_config      0x03
#define pca9555_reg_input_port0  0x00
#define pca9555_reg_input_port1  0x01
#define pca9555_reg_output_port0 0x02
#define pca9555_reg_output_port1 0x03
#define pca9555_reg_config_port0 0x06
#define pca9555_reg_config_port1 0x07

/* init.bas pattern: try PCA955x, then PCA955xA; on success set the ready flag */
static __attribute__((unused)) uint8_t detect(uint8_t adr, uint8_t adr_a, uint8_t rdy_bit)
{
	uint8_t found = 0;
	if (!(channel_open & (1u << 5)))
		return 0;
	twi_start(1);
	twi_wbyte(1, adr);
	if (i2c_err == 0) {
		found = adr;
		hardware_other |= 1u << rdy_bit;
	} else {
		twi_stop(1);
		delay_ms(5);
		twi_start(1);
		twi_wbyte(1, adr_a);
		if (i2c_err == 0) {
			found = adr_a;
			hardware_other |= 1u << rdy_bit;
		} else {
			errorcount((uint8_t)(rdy_bit + BAS_ERRORCOUNT_OFFSET_OTHER));
		}
	}
	twi_stop(1);
	return found;
}

#if BAS_MODUL_PCA9554 == 1 && BAS_MODUL_PCA9554_41 == 1
static uint8_t pca9554_x1_adr;
static void pca9554_41_init_register(void)
{
	if (!(hardware_other & (1u << BAS_PCA9554_41_RDY)))
		return;
	i2c_device_addr = pca9554_x1_adr;
	i2c_reg = pca9554_reg_config;
	buffer_byte = BAS_MODUL_PCA9554_41_CONFIG;
	i2c_wbyte(1);
	i2c_reg = pca9554_reg_input_port;
	i2c_rbyte(1);
}
#endif

#if BAS_MODUL_PCA9554 == 1 && BAS_MODUL_PCA9554_4F == 1
static uint8_t pca9554_xF_adr;
static void pca9554_4F_init_register(void)
{
	if (!(hardware_other & (1u << BAS_PCA9554_4F_RDY)))
		return;
	i2c_device_addr = pca9554_xF_adr;
	i2c_reg = pca9554_reg_config;
	buffer_byte = BAS_MODUL_PCA9554_4F_CONFIG;
	i2c_wbyte(1);
	i2c_reg = pca9554_reg_output_port;
	buffer_byte = BAS_MODUL_PCA9554_4F_OUT_DEFAULT;
	i2c_wbyte(1);
	i2c_reg = pca9554_reg_input_port;
	i2c_rbyte(1);
}

void pca9554_4F_set_output(void)
{
	if (!(hardware_other & (1u << BAS_PCA9554_4F_RDY)))
		return;
	i2c_device_addr = pca9554_xF_adr;
	i2c_reg = pca9554_reg_output_port;
	i2c_rbyte(1);
	buffer_byte = (uint8_t)((buffer_byte & ~pca9554_bits) | (pca9554_value & pca9554_bits));
	i2c_device_addr = pca9554_xF_adr;
	i2c_reg = pca9554_reg_output_port;
	i2c_wbyte(1);
}
#else
void pca9554_4F_set_output(void) {}
#endif

#if BAS_MODUL_PCA9555 == 1 && BAS_MODUL_PCA9555_4F == 1
static uint8_t pca9555_xF_adr;
static void pca9555_4F_init_register(void)
{
	if (!(hardware_other & (1u << BAS_PCA9555_4F_RDY)))
		return;
	buffer_word_set(BAS_MODUL_PCA9555_4F_CONFIG);
	i2c_device_addr = pca9555_xF_adr;
	i2c_reg = pca9555_reg_config_port0;
	i2c_wbyte(1);
	i2c_reg = pca9555_reg_config_port1;
	buffer_byte = buffer_bytes[2];
	i2c_wbyte(1);
	buffer_word_set(BAS_MODUL_PCA9555_4F_OUT_DEFAULT);
	i2c_reg = pca9555_reg_output_port0;
	i2c_wbyte(1);
	i2c_reg = pca9555_reg_output_port1;
	buffer_byte = buffer_bytes[2];
	i2c_wbyte(1);
	i2c_reg = pca9555_reg_input_port1;
	i2c_rbyte(1);
	buffer_bytes[2] = buffer_byte;
	i2c_reg = pca9555_reg_input_port0;
	i2c_rbyte(1);
}

void pca9555_4F_set_output(void)
{
	if (!(hardware_other & (1u << BAS_PCA9555_4F_RDY)))
		return;
	i2c_device_addr = pca9555_xF_adr;
	i2c_reg = pca9555_reg_output_port1;
	i2c_rbyte(1);
	buffer_bytes[2] = buffer_byte;
	i2c_reg = pca9555_reg_output_port0;
	i2c_rbyte(1);
	buffer_word_set((uint16_t)((buffer_word_get() & ~pca9555_bits) | (pca9555_value & pca9555_bits)));
	i2c_device_addr = pca9555_xF_adr;
	i2c_reg = pca9555_reg_output_port0;
	i2c_wbyte(1);
	i2c_reg = pca9555_reg_output_port1;
	buffer_byte = buffer_bytes[2];
	i2c_wbyte(1);
}
#else
void pca9555_4F_set_output(void) {}
#endif

void expander_init(void)
{
	/* order of libs/expander/init.bas: pca9554 (41, 4F), then pca9555 (4F) */
#if BAS_MODUL_PCA9554 == 1 && BAS_MODUL_PCA9554_41 == 1
	pca9554_x1_adr = detect(0x40, 0x70, BAS_PCA9554_41_RDY);
	pca9554_41_init_register();
#endif
#if BAS_MODUL_PCA9554 == 1 && BAS_MODUL_PCA9554_4F == 1
	pca9554_xF_adr = detect(0x4E, 0x7E, BAS_PCA9554_4F_RDY);
	pca9554_4F_init_register();
#endif
#if BAS_MODUL_PCA9555 == 1 && BAS_MODUL_PCA9555_4F == 1
	pca9555_xF_adr = detect(BAS_PCA9555_4F_ADR_W, BAS_PCA9555_7F_ADR_W, BAS_PCA9555_4F_RDY);
	pca9555_4F_init_register();
#endif
}
