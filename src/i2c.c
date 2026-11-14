/*
 * Port of libs/interface/V40/5/functions.bas and /6 (identical for bus 2).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "i2c.h"
#include "bas_data.h"
#include "registers.h"

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

/* PORT (#66): len bytes from addr of a 24xx512 into dst (forward); 1 = the chip answered */
uint8_t i2c_ee_read(uint8_t bus, uint8_t dev, uint16_t addr, uint8_t *dst, uint8_t len)
{
	twi_start(bus);
	twi_wbyte(bus, dev);
	twi_wbyte(bus, (uint8_t)(addr >> 8));
	twi_wbyte(bus, (uint8_t)addr);
	twi_repstart(bus);
	twi_wbyte(bus, (uint8_t)(dev + 1));
	uint8_t ok = i2c_err == 0;
	for (uint8_t i = 0; i < len; i++)
		dst[i] = twi_rbyte(bus, (uint8_t)(i + 1 < len));
	twi_stop(bus);
	return ok;
}

/* page writes of the 24xx512: a new write cycle at every 128-byte boundary */
static void wbuffer_eeprom(uint8_t bus)
{
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

/* PORT (#66): every write into an EEPROM is read back and repeated on a difference (twice at most);
 * a bit error would otherwise stay in the stored tuning. The buffer goes out backwards: address
 * a + i holds buffer_bytes(size - i). */
void i2c_wbuffer_eeprom(uint8_t bus)
{
	if (!OPEN(bus))
		return;
	const uint8_t dev = i2c_device_addr, size = i2c_size;
	const uint16_t addr = i2c_addr;
	for (uint8_t attempt = 0; attempt < 3; attempt++) {
		i2c_device_addr = dev; i2c_addr = addr; i2c_size = size;
		wbuffer_eeprom(bus);
		uint8_t back[32], same = 1;
		for (uint8_t off = 0; off < size && same; off += sizeof back) {
			uint8_t left = (uint8_t)(size - off), n = left < (uint8_t)sizeof back ? left : (uint8_t)sizeof back;
			if (!i2c_ee_read(bus, dev, (uint16_t)(addr + off), back, n))
				same = 0;
			for (uint8_t k = 0; k < n && same; k++)
				same = back[k] == buffer_bytes[size - (off + k)];
		}
		if (same)
			goto done;
		i2c_reset(bus);
	}
	errorcount(BAS_ERRORCOUNT_SW_EEPROM);
done:
	i2c_device_addr = dev; i2c_addr = addr; i2c_size = 0;
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

void i2c_wbyte_sequence(uint8_t bus, const uint8_t *table)
{
	bas_reader r = BAS_RESTORE(table);
	uint16_t data_size = (uint16_t)(bas_read_w(&r) / 3);
	for (uint16_t w = 1; w <= data_size; w++) {
		i2c_reg = bas_read_b(&r);
		uint8_t value = bas_read_b(&r);
		uint8_t mask = bas_read_b(&r);
		if (mask == 0x00)
			continue;
		if (mask == 0xFF) {
			buffer_byte = value;
			i2c_wbyte(bus);
		} else {
			value &= mask;
			i2c_rbyte(bus);
			buffer_byte = (uint8_t)((buffer_byte & (uint8_t)~mask) | value);
			i2c_wbyte(bus);
		}
	}
}
