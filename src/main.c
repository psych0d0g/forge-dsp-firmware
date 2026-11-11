/*
 * Forge DSP firmware in C, all flavours. State: M0 bring-up, M1 protocol core, M2 registers/EEPROM.
 *   clock, pins and supply hold as the BASCOM firmware, watchdog 8 s,
 *   4 serial channels with the BASCOM parser, system.{boot,echo,firmware,info,key,ping}.
 * Safe state: peripherals stay in reset, remote out (amplifiers) stays off.
 * License: CC BY-NC-SA 4.0
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include "board.h"
#include "clock.h"
#include "cmd.h"
#include "iface.h"
#include "mcuid.h"
#include "power.h"
#include "registers.h"
#include "i2c.h"
#include "store.h"
#include "expander.h"
#include "si5338.h"
#include "flavour_systeminit.h"
#include "api.h"
#include "events.h"
#include "loopcall.h"
#include "modc.h"
#include <util/delay.h>
#include "tick.h"
#include "version.h"
#include "watchdog.h"

/* found by chip45boot3 CMD_READ_VERSION_FIRMWARE */
const char fw_version_c45b3[] PROGMEM __attribute__((used)) = FW_VERSION_C45B3;

/* hooks of the generated systeminit sequence */
void interrupts_enable(void) { sei(); }
void watchdog_start(void) { watchdog_enable_8s(); }
void watchdog_kick(void) { watchdog_reset(); }

/* router hooks */
uint8_t iface_hook_ir_master_online(void)
{
	return (registers[BAS_REG_EXT_IRSETUP] >> BAS_IRRECEIVER_STATE_ON) & 1;
}
uint8_t iface_hook_usb_dtr(void) { return PIN_READ(PIN_EXT_USB_DTR); }
void iface_hook_busy(uint8_t on) { if (on) PIN_SET(PIN_LED_YELLOW); else PIN_CLR(PIN_LED_YELLOW); }
void iface_hook_watchdog(void) { watchdog_reset(); }

int main(void)
{
	/* referenced so --gc-sections keeps the version string */
	__asm__ __volatile__("" :: "r"(fw_version_c45b3));
	clock_init();
	board_init();
	power_hold();
	NVM.CTRLB |= NVM_EEMAPEN_bm;  /* config eeprom = mapped */
	/* systeminit.bas: "wait a bit to use the eeprom values right" */
	_delay_ms(250);
	errorcount_init();
	registers_load();
	sysinit_check(PIN_READ(PIN_DBG_INIT_FINEST));
	mcuid_init();
	tick_init();
	iface_init();
	i2c_bus_init(1);   /* libs/interface/V40/5/init.bas: open "twic", 100 kHz */
	i2c_bus_init(2);   /* .../6/init.bas: open "twie" */
#if FORGE_BRINGUP && FLAVOUR_SYSTEMINIT_COMPLETE
	/* peripheral part of systeminit.bas (generated): clock, store, expanders and their
	 * power-up bits, DAC (muted), DSP program EEPROMs, peripheral resets, DSP cores, SPDIF */
	flavour_systeminit_peripherals();
	/* libs/api/init.bas, then the preset load of modc_load_preset (MODC itself follows) */
	api_init();
	api_init_mute();
	ctrl_events_init();   /* libs/ctrl/events init parts: fields, shutdown, usb, grounds, overflow */
	events_poll_pins();
	if (PIN_READ(PIN_OPTION_SPDIF_4CH) == 0x00)
		events_spdif_init();
	ctrl_modc_init();     /* blocks from the EEPROM; requests the preset load */
#else
	/* safe state: only detection; peripheral resets held, power-up bits off */
	si5338_detect_and_init();
	at24c512_detect();
	store_init();
	expander_init();
#endif
	cmd_init();
	/* config priority = roundrobin, lo/med/hi enabled, vectors in application */
	CCP = CCP_IOREG_gc;
	PMIC.CTRL = PMIC_RREN_bm | PMIC_LOLVLEN_bm | PMIC_MEDLVLEN_bm | PMIC_HILVLEN_bm;
	watchdog_enable_8s();          /* config watchdog = 8000 */
	sei();

	PIN_CLR(PIN_LED_RED);
	sysinit_count_boot();

	uint16_t last = tick_ms();
	for (;;) {
		watchdog_reset();
		uint16_t now = tick_ms();
#if FORGE_BRINGUP && FLAVOUR_SYSTEMINIT_COMPLETE
		loopcall(loopcall_take());
#else
		cmd_check4buffer();
#endif
#if FORGE_BRINGUP && FLAVOUR_SYSTEMINIT_COMPLETE
		/* shutdown is a MODC block (delays from its sets); fallback if none is enabled */
		if (!modc_shutdown_enabled())
#endif
		power_poll(now);
		if ((uint16_t)(now - last) >= 500) {
			last = now;
			PIN_TGL(PIN_LED_GREEN);
		}
	}
}
