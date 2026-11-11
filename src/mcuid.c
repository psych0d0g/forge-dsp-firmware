/*
 * MCU id as libs/misc/mcuid/functions.bas (init_mcuid): production signature row
 * bytes 0x08..0x0D as raw characters, then hex of 0x10 and 0x12..0x15.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include <avr/io.h>
#include <stddef.h>
#include "mcuid.h"

char mcuid_string[17];

static uint8_t readsig(uint8_t index)
{
	uint8_t v;
	NVM.CMD = NVM_CMD_READ_CALIB_ROW_gc;
	__asm__ __volatile__("lpm %0, Z" : "=r"(v) : "z"((uint16_t)index));
	NVM.CMD = NVM_CMD_NO_OPERATION_gc;
	return v;
}

static void hex2(char *d, uint8_t v)
{
	static const char h[] = "0123456789ABCDEF";
	d[0] = h[v >> 4];
	d[1] = h[v & 15];
}

void mcuid_init(void)
{
	uint8_t a = 0;
	for (uint8_t i = 0x08; i <= 0x0D; i++)
		mcuid_string[a++] = (char)readsig(i);   /* chr(b); a 0 byte ends the string like in BASCOM */
	hex2(&mcuid_string[a], readsig(0x10));
	a += 2;
	for (uint8_t i = 0x12; i <= 0x15; i++, a += 2)
		hex2(&mcuid_string[a], readsig(i));
	mcuid_string[16] = 0;
}
