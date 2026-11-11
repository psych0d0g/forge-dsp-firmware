/*
 * Port of libs/loopcall/functions.bas.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "loopcall.h"
#include "api.h"
#include "board.h"
#include "cmd.h"
#include "events.h"
#include "modc.h"
#include "ir.h"
#include "registers.h"
#include "store.h"
#include "tick.h"

void sys_init_mainloop(void);
void events_vumeter_mainloop(void);
void watchdog_kick(void);

#define B(n) (bits & (1u << (n)))

void loopcall(uint16_t bits)
{
	events_poll_pins();  /* PORT: pin interrupts of the original */
	if (B(timer_tce0_div_256)) {
		cmd_check4buffer();
		irreceiver_mainloop();
	}
	if (B(timer_tce0_div_32)) {
		events_adc_mainloop_32();
		ctrl_modc_mainloop();
	}
	if (B(timer_tce0_div_16)) {
		events_adc_mainloop_16();
		sys_init_mainloop();
		events_overflow_mainloop();
	}
	if (B(timer_tce0_div_8)) {
		api_mainloop();
		store_mainloop();
#if BAS_MODUL_CS8416 > 0
		/* V30-96LN: spdif events at 1/8 s */
		if (PIN_READ(PIN_OPTION_SPDIF_4CH) == 0x00)
			events_spdif_mainloop();
#endif
	}
	if (B(timer_tce0_div_2)) {
		if (error_count > 0)
			PIN_TGL(PIN_LED_RED);
	}
	if (B(timer_tce0_div_1)) {
		events_vumeter_mainloop();
#if BAS_MODUL_CS8416 == 0
		if (PIN_READ(PIN_OPTION_SPDIF_4CH) == 0x00)
			events_spdif_mainloop();
#endif
		watchdog_kick();
	}
	if (B(timer_tce0_div_41))
		events_adc_mainloop_41();
}
