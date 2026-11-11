/*
 * Host backend of the TWI primitives: a 24xx512 model (page writes wrap inside 128-byte
 * pages like the real chip) on bus 1 at 0xA8, and a log of all transfers.
 *   "#i2clog=1" in the input prints every transfer as  @i2c<bus>: S 50 .. P
 */
#include <stdio.h>
#include <string.h>
#include "i2c.h"

uint8_t i2c_err;
int host_i2c_log;
uint8_t at24[65536];
static uint8_t cur_dev[3], state[3];  /* state: 0 idle, 1 addr hi, 2 addr lo, 3 data */
static uint16_t ptr;
static int logged[3];

void delay_ms(uint16_t ms) { (void)ms; }
void i2c_bus_init(uint8_t bus) { channel_open |= 1u << (4 + bus); }
uint8_t twi_sda(uint8_t bus) { (void)bus; return 1; }
void twi_bus_clear(uint8_t bus) { (void)bus; }

static void lg(uint8_t bus, const char *fmt, unsigned v)
{
	if (!host_i2c_log)
		return;
	if (!logged[bus]) { printf("@i2c%u:", bus); logged[bus] = 1; }
	printf(fmt, v);
}

void twi_start(uint8_t bus) { state[bus] = 0xFE; i2c_err = 0; lg(bus, " S%.0u", 0); }
void twi_repstart(uint8_t bus) { state[bus] = 0xFE; lg(bus, " Sr%.0u", 0); }

void twi_wbyte(uint8_t bus, uint8_t b)
{
	lg(bus, " %02X", b);
	if (state[bus] == 0xFE) {
		cur_dev[bus] = b;
		state[bus] = (b & 1) ? 4 : 1;
		if ((b & 0xFE) != 0xA8 || bus != 1)
			i2c_err = 1;  /* nobody else on the simulated bus */
		return;
	}
	if (cur_dev[bus] != 0xA8 || bus != 1)
		return;
	switch (state[bus]) {
	case 1: ptr = (uint16_t)(b << 8); state[bus] = 2; break;
	case 2: ptr |= b; state[bus] = 3; break;
	case 3:
		at24[ptr] = b;
		ptr = (uint16_t)((ptr & ~0x7F) | ((ptr + 1) & 0x7F));  /* wrap inside the page */
		break;
	}
}

uint8_t twi_rbyte(uint8_t bus, uint8_t ack)
{
	uint8_t v = 0xFF;
	if (cur_dev[bus] == 0xA9 && bus == 1)
		v = at24[ptr++];  /* sequential read crosses pages */
	lg(bus, " r%02X", v);
	if (!ack)
		lg(bus, "N%.0u", 0);
	return v;
}

void twi_stop(uint8_t bus)
{
	lg(bus, " P%.0u", 0);
	if (host_i2c_log && logged[bus]) { printf("\n"); logged[bus] = 0; }
	state[bus] = 0;
}
