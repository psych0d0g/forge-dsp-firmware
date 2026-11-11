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
#include "iface.h"
#include "modc.h"

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

void mcu_reset_delayed(void)
{
	/* mcu_reset (libs/mcu/xmegaA3): debug_close, waitms 1000, software reset */
	for (uint8_t b = 0; b < 4; b++)
		iface_flush(b);
	for (uint8_t i = 0; i < 10; i++) {
		watchdog_reset();
		_delay_ms(100);
	}
	mcu_reset();
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

static void led_wait(uint8_t n, uint8_t yellow_red)
{
	for (uint8_t i = 1; i <= n; i++) {
		if (yellow_red) { PIN_TGL(PIN_LED_YELLOW); PIN_TGL(PIN_LED_RED); }
		else { PIN_TGL(PIN_LED_GREEN); PIN_TGL(PIN_LED_YELLOW); }
		_delay_ms(250);
		watchdog_reset();
	}
}

/* modc_shutdown: sets 3/4 = waits (250 ms steps) before remote out off / before power off */
void power_shutdown(uint8_t wait_before_remote_off, uint8_t wait_before_power_off)
{
	_delay_ms(100);
	for (uint8_t b = 0; b < 4; b++)
		iface_flush(b);  /* debug_close */
	_delay_ms(100);
	cli();
	if (wait_before_remote_off > 0) {  /* led_gruen_gelb_wait */
		PIN_CLR(PIN_LED_GREEN);
		PIN_SET(PIN_LED_YELLOW);
		led_wait(wait_before_remote_off, 0);
		PIN_SET(PIN_LED_GREEN);
		PIN_CLR(PIN_LED_YELLOW);
	}
	PIN_CLR(PIN_LED_GREEN);
	PIN_CLR(PIN_CPU_POWER_CTRL_OUT);
	if (wait_before_power_off > 0) {   /* led_gelb_rot_wait */
		PIN_CLR(PIN_LED_YELLOW);
		PIN_SET(PIN_LED_RED);
		led_wait(wait_before_power_off, 1);
		PIN_SET(PIN_LED_YELLOW);
	}
	shutdown();
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
