/*
 * PORT extension (#67): time of each start phase, see include/boottime.h.
 * License: CC BY-NC-SA 4.0
 */
#include "boottime.h"
#include "eram.h"
#include "flavour_eram.h"
#ifdef __AVR__
#include <avr/io.h>
#else
#include "tick.h"
#endif

static uint16_t at[BT_COUNT];  /* 0 = not reached (a phase at 0 ms counts as 1 ms) */

void boot_time_start(void)
{
#ifdef __AVR__
	/* chip45boot3 runs the RTC at 1.024 kHz while it waits for a host (up to 60000 counts) and only
	 * stops it when it starts the application: the count is the time spent there (0 = jumped
	 * directly, the RTC is never started on that path) */
	uint16_t bl = RTC.CNT;
	at[BT_BOOTLOADER] = (uint16_t)((((uint32_t)bl * 1000u) >> 10) + 1);
	/* the 32.768 kHz RC runs (clock_init); RTC source: its 1.024 kHz output */
	CLK.RTCCTRL = CLK_RTCSRC_RCOSC_gc | CLK_RTCEN_bm;
	while (RTC.STATUS & RTC_SYNCBUSY_bm)
		;
	RTC.PER = 0xFFFF;
	RTC.CNT = 0;
	RTC.CTRL = RTC_PRESCALER_DIV1_gc;
#endif
}

static uint16_t now_ms(void)
{
#ifdef __AVR__
	uint16_t c = RTC.CNT;
	return (uint16_t)(((uint32_t)c * 1000u) >> 10);  /* 1024 per second */
#else
	return tick_ms();
#endif
}

void boot_mark(uint8_t phase)
{
	if (phase < BT_COUNT && at[phase] == 0) {
		uint16_t t = now_ms();
		at[phase] = t == 0 ? 1 : t == 0xFFFF ? 0xFFFE : t;
	}
}

/* chip45boot3 stays in the bootloader while EEPROM 0/1 = AA 55 (system.boot writes it) and clears
 * it only when it times out (58.6 s without a host), not when the host starts the application after
 * an update. Left set, the next power-on waited the full 58.6 s before the firmware ran. */
uint8_t boot_magic_clear(void)
{
	if (eram_rb(ERAM_EEPROM_BOOTLOADER_SEQ_1) != 0xAA || eram_rb(ERAM_EEPROM_BOOTLOADER_SEQ_2) != 0x55)
		return 0;
	eram_wb(ERAM_EEPROM_BOOTLOADER_SEQ_1, 0xFF);
	eram_wb(ERAM_EEPROM_BOOTLOADER_SEQ_2, 0xFF);
	return 1;
}

uint16_t boot_time_ms(uint8_t phase) { return phase < BT_COUNT && at[phase] ? at[phase] : 0xFFFF; }
