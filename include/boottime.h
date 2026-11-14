/*
 * PORT extension (#67): time of each start phase, for get system.startup.time.
 * The RTC counts from the internal 32.768 kHz RC (1.024 kHz) from clock_init on, independent of
 * interrupts (the tick only runs after the systeminit enables them) and without wrap for 64 s.
 * License: CC BY-NC-SA 4.0
 */
#ifndef BOOTTIME_H_
#define BOOTTIME_H_
#include <stdint.h>

enum {
	BT_REGISTERS = 1,  /* EEPROM settings loaded (after the 250 ms of main.bas) */
	BT_CLOCK,          /* Si5338 */
	BT_STORE,          /* preset EEPROM */
	BT_EXPANDER,
	BT_DAC,            /* after the 500 ms power-up wait of the systeminit */
	BT_DSP_EEPROM1,    /* program checksum of DSP EEPROM 1..3 (rewritten if wrong) */
	BT_DSP_EEPROM2,
	BT_DSP_EEPROM3,
	BT_DSP_BOOT_BUS1,  /* the TAS3108 on bus 1 have loaded their program (SDA quiet) */
	BT_DSP_CORE1,
	BT_SPDIF,
	BT_DSP_BOOT_BUS2,
	BT_DSP_CORE2,
	BT_DSP_CORE3,
	BT_API,            /* hardware check, limiter, api init */
	BT_SD,             /* microSD mounted (or none) */
	BT_INIT_DONE,      /* MODC blocks, event system.boot=ok */
	BT_PRESET,         /* first preset in the DSPs */
	BT_REMOTE_ON,      /* remote out on (after modc.32 set 1) */
	BT_UNMUTED,        /* outputs free (after modc.32 set 3) */
	BT_BOOTLOADER,     /* time in the bootloader before the application (its RTC count), 0 = direct */
	BT_COUNT
};

void boot_time_start(void);
uint8_t boot_magic_clear(void);    /* PORT (#67): EEPROM 0/1 = AA 55 left by system.boot -> 0xFF; 1 = it was set */
void boot_mark(uint8_t phase);       /* first time only */
uint16_t boot_time_ms(uint8_t phase); /* 0xFFFF = not reached */
#endif
