/*
 * Port of the "checks evaluate" step at the end of systeminit.bas: if the peripherals found
 * on the I2C buses (hardware_dsp, hardware_other) differ from the variant's chk_hardware_*,
 * the outputs stay muted with the hardware priority (api_mute_out_prio_hardware).
 * PORT fix (#32): V30-96ELN expects the clock generator (bit 11), which that build does not
 * have (modul_si5338 = 0), and not the ES9018 (bit 2) and CT7302 #20 (bit 3) it detects; the
 * original therefore always muted. The port expects 0x007D there (V40-96ELN without Si5338).
 * PORT fix (#45): if DSP cores are still missing, restart once (software reset), as the
 * original does in effect through its watchdog (it waits for the program load without a
 * limit). An EEPROM flag allows only one restart in a row, so a missing DSP cannot loop.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "hwcheck.h"
#include "api.h"
#include "flavour.h"
#include "store.h"
#include "tas3108.h"
#include "eram.h"

#define EE_DSP_RESTART 0x0F11  /* byte: 1 = the last start restarted for missing DSP cores */
uint8_t hw_dsp_restarted;
void mcu_reset(void);  /* power.c: software reset */

uint16_t hw_check_expected_other(void)
{
#if BAS_MODUL_SI5338 == 0 && (BAS_CHK_HARDWARE_OTHER == 0x0871)
	return 0x007D;  /* V30-96ELN, see above */
#else
	return BAS_CHK_HARDWARE_OTHER;
#endif
}

uint16_t hw_check_expected_dsp(void)
{
	return BAS_CHK_HARDWARE_DSP;
}

uint8_t hw_check_evaluate(void)
{
	uint8_t bad = 0;
	if (hardware_dsp != hw_check_expected_dsp())
		bad = 1;
	if (hardware_other != hw_check_expected_other())
		bad = 1;
	if (bad)
		api_set_out_mute_prio(api_mute_out_prio_hardware, api_mute_out_on);
	return bad;
}

void hw_check_dsp_restart(void)
{
	uint8_t f = eram_rb(EE_DSP_RESTART);
	hw_dsp_restarted = f == 1;
	if (hardware_dsp == hw_check_expected_dsp() || f == 1) {
		eram_wb(EE_DSP_RESTART, 0xFF);  /* found, or already restarted once: go on */
		return;
	}
	eram_wb(EE_DSP_RESTART, 1);
	mcu_reset();
}
