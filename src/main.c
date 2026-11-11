/*
 * Forge DSP 96ELN-3 firmware in C, milestone M0 (bring-up):
 *   clock, pins and supply hold as the BASCOM firmware, watchdog 8 s,
 *   USB-COM 38400 with <system.firmware and >system.bootloader=1.
 * Safe state: peripherals stay in reset, remote out (amplifiers) stays off,
 * so the DSP path is silent. Green LED blinks, yellow = M0.
 * License: CC BY-NC-SA 4.0
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include "watchdog.h"
#include "board.h"
#include "clock.h"
#include "cmd.h"
#include "mcuid.h"
#include "power.h"
#include "tick.h"
#include "uart.h"
#include "version.h"

#include <avr/pgmspace.h>
/* found by chip45boot3 CMD_READ_VERSION_FIRMWARE */
const char fw_version_c45b3[] PROGMEM __attribute__((used)) = FW_VERSION_C45B3;

int main(void)
{
	/* referenced so --gc-sections keeps the version string */
	__asm__ __volatile__("" :: "r"(fw_version_c45b3));
	clock_init();
	board_init();
	mcuid_init();
	power_hold();
	NVM.CTRLB |= NVM_EEMAPEN_bm;  /* config eeprom = mapped */
	tick_init();
	uart_init();
	/* config priority = roundrobin, lo/med/hi enabled, vectors in application */
	CCP = CCP_IOREG_gc;
	PMIC.CTRL = PMIC_RREN_bm | PMIC_LOLVLEN_bm | PMIC_MEDLVLEN_bm | PMIC_HILVLEN_bm;
	watchdog_enable_8s();          /* config watchdog = 8000 */
	sei();

	PIN_CLR(PIN_LED_RED);
	PIN_SET(PIN_LED_YELLOW);

	uint16_t last = tick_ms();
	for (;;) {
		watchdog_reset();
		uint16_t now = tick_ms();
		cmd_poll();
		power_poll(now);
		if ((uint16_t)(now - last) >= 500) {
			last = now;
			PIN_TGL(PIN_LED_GREEN);
		}
	}
}
