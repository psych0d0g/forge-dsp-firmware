/*
 * Port of the "checks evaluate" step at the end of systeminit.bas: if the peripherals found
 * on the I2C buses (hardware_dsp, hardware_other) differ from the variant's chk_hardware_*,
 * the outputs stay muted with the hardware priority (api_mute_out_prio_hardware).
 * PORT fix (#32): V30-96ELN expects the clock generator (bit 11), which that build does not
 * have (modul_si5338 = 0), and not the ES9018 (bit 2) and CT7302 #20 (bit 3) it detects; the
 * original therefore always muted. The port expects 0x007D there (V40-96ELN without Si5338).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "hwcheck.h"
#include "api.h"
#include "flavour.h"
#include "store.h"
#include "tas3108.h"

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
