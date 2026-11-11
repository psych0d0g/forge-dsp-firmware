/*
 * Port of libs/interface/V40/5/functions.bas and /6 (identical for bus 2).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "i2c.h"

uint8_t buffer_bytes[BAS_BUFFER_BYTES_MAX + 1];
uint8_t i2c_device_addr;
uint16_t i2c_addr;
uint8_t i2c_size;
uint16_t channel_open;

#define OPEN(bus) (channel_open & (1u << (4 + (bus))))  /* channel_open.5 / .6 */

void i2c_reset(uint8_t bus)
{
	if (twi_sda(bus) == 0)
		twi_bus_clear(bus);  /* TWI off, 8 x SCL low/high (1 us), SDA high, TWI on */
}

void i2c_wbyte(uint8_t bus)
{
	if (!OPEN(bus))
		return;
	twi_start(bus);
	twi_wbyte(bus, i2c_device_addr);
	twi_wbyte(bus, i2c_reg);
	twi_wbyte(bus, buffer_byte);
	twi_stop(bus);
}

void i2c_wbuffer(uint8_t bus)
{
	if (!OPEN(bus))
		return;
	twi_start(bus);
	twi_wbyte(bus, i2c_device_addr);
	twi_wbyte(bus, i2c_reg);
	do {
		twi_wbyte(bus, buffer_bytes[i2c_size]);
		i2c_size--;
	} while (i2c_size != 0);
	twi_stop(bus);
}

void i2c_wbyte_highreg(uint8_t bus)
{
	if (!OPEN(bus))
		return;
	twi_start(bus);
	twi_wbyte(bus, i2c_device_addr);
	twi_wbyte(bus, i2c_reg_h);
	twi_wbyte(bus, i2c_reg_l);
	twi_wbyte(bus, buffer_byte);
	twi_stop(bus);
}

void i2c_wbuffer_highreg(uint8_t bus)
{
	if (!OPEN(bus))
		return;
	twi_start(bus);
	twi_wbyte(bus, i2c_device_addr);
	twi_wbyte(bus, i2c_reg_h);
	twi_wbyte(bus, i2c_reg_l);
	do {
		twi_wbyte(bus, buffer_bytes[i2c_size]);
		i2c_size--;
	} while (i2c_size != 0);
	twi_stop(bus);
}

void i2c_wbyte_eeprom(uint8_t bus)
{
	i2c_wbyte_highreg(bus);
	delay_ms(BAS_EEPROM_PAGE_WRITE_DELAY);
}

/* page writes of the 24xx512: a new write cycle at every 128-byte boundary */
void i2c_wbuffer_eeprom(uint8_t bus)
{
	if (!OPEN(bus))
		return;
	twi_start(bus);
	twi_wbyte(bus, i2c_device_addr);
	twi_wbyte(bus, i2c_reg_h);
	twi_wbyte(bus, i2c_reg_l);
	for (;;) {
		twi_wbyte(bus, buffer_bytes[i2c_size]);
		i2c_size--;
		if (i2c_size == 0)
			break;
		i2c_reg_l++;
		if (i2c_reg_l % BAS_I2C_WBUFFER_EEPROM_SIZE == 0) {
			twi_stop(bus);
			delay_ms(BAS_EEPROM_PAGE_WRITE_DELAY);
			if (i2c_reg_l == 0x00)
				i2c_reg_h++;
			twi_start(bus);
			twi_wbyte(bus, i2c_device_addr);
			twi_wbyte(bus, i2c_reg_h);
			twi_wbyte(bus, i2c_reg_l);
		}
	}
	twi_stop(bus);
	delay_ms(BAS_EEPROM_PAGE_WRITE_DELAY);
}

void i2c_rbyte(uint8_t bus)
{
	if (!OPEN(bus))
		return;
	twi_start(bus);
	twi_wbyte(bus, i2c_device_addr);
	twi_wbyte(bus, i2c_reg);
	twi_repstart(bus);
	twi_wbyte(bus, (uint8_t)(i2c_device_addr + 1));
	buffer_byte = twi_rbyte(bus, 0);
	twi_stop(bus);
	i2c_reset(bus);
}

static void read_backwards(uint8_t bus)
{
	do {
		buffer_bytes[i2c_size] = twi_rbyte(bus, 1);
		i2c_size--;
	} while (i2c_size != 1);
	buffer_bytes[1] = twi_rbyte(bus, 0);
	twi_stop(bus);
	i2c_reset(bus);
}

void i2c_rbuffer(uint8_t bus)
{
	if (!OPEN(bus))
		return;
	twi_start(bus);
	twi_wbyte(bus, i2c_device_addr);
	twi_wbyte(bus, i2c_reg);
	twi_repstart(bus);
	twi_wbyte(bus, (uint8_t)(i2c_device_addr + 1));
	read_backwards(bus);  /* PORT: i2c_size must be >= 2 here, as in BASCOM */
}

void i2c_rbyte_highreg(uint8_t bus)
{
	if (!OPEN(bus))
		return;
	twi_start(bus);
	twi_wbyte(bus, i2c_device_addr);
	twi_wbyte(bus, i2c_reg_h);
	twi_wbyte(bus, i2c_reg_l);
	twi_repstart(bus);
	twi_wbyte(bus, (uint8_t)(i2c_device_addr + 1));
	buffer_byte = twi_rbyte(bus, 0);
	twi_stop(bus);
	i2c_reset(bus);
}

void i2c_rbuffer_highreg(uint8_t bus)
{
	if (!OPEN(bus))
		return;
	twi_start(bus);
	twi_wbyte(bus, i2c_device_addr);
	twi_wbyte(bus, i2c_reg_h);
	twi_wbyte(bus, i2c_reg_l);
	twi_repstart(bus);
	twi_wbyte(bus, (uint8_t)(i2c_device_addr + 1));
	read_backwards(bus);
}
