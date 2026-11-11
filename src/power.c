/*
 * Supply handling as systeminit.bas / ctrl_shutdown (BASCOM 1.11.6000):
 * PF6 high = MCU holds its own supply, PF7 reads 0 = remote in gone (input is
 * inverted by PIN7CTRL = 0x58, the logic follows BASCOM "if cpu_power_ctrl_in = 0"),
 * PB7 = remote out (amplifiers), PF4/PF5 = peripheral reset (low = reset).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include "watchdog.h"
#include <util/delay.h>
#include "board.h"
#include "power.h"
#include "uart.h"

#define SHUTDOWN_DEBOUNCE_MS 50

void power_hold(void)
{
	PIN_SET(PIN_CPU_POWER_CTRL_OFF);  /* "set cpu_power_ctrl_off" */
}

void mcu_reset(void)
{
	cli();
	CCP = CCP_IOREG_gc;
	RST.CTRL = RST_SWRST_bm;
	for (;;)
		;
}

static void shutdown(void)
{
	/* order of ctrl_shutdown: remote out off, peripherals into reset,
	 * release own supply, all LEDs on, reset (if the supply stays) */
	cli();
	PIN_CLR(PIN_LED_GREEN);
	PIN_CLR(PIN_CPU_POWER_CTRL_OUT);
	PIN_CLR(PIN_PERIPHERAL_DEVICES_RESET1);
	PIN_CLR(PIN_PERIPHERAL_DEVICES_RESET2);
	PIN_CLR(PIN_CPU_POWER_CTRL_OFF);
	PIN_SET(PIN_LED_GREEN);
	PIN_SET(PIN_LED_YELLOW);
	PIN_SET(PIN_LED_RED);
	watchdog_disable();
	mcu_reset();
}

void power_poll(uint16_t ms_now)
{
	static uint16_t low_since;
	static uint8_t low;

	if (PIN_READ(PIN_CPU_POWER_CTRL_IN)) {
		low = 0;
		return;
	}
	if (!low) {
		low = 1;
		low_since = ms_now;
	} else if ((uint16_t)(ms_now - low_since) >= SHUTDOWN_DEBOUNCE_MS) {
		shutdown();
	}
}
