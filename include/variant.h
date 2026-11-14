/*
 * PORT (#69): one build for every Forge DSP 96ELN and ETON CORE D8. The 96ELN with PCB 3.0 (V30)
 * differs from the V40 boards only in the clock generator Si5338: same pins, same DSP programs.
 * The builds with Si5338 and ES9018 (V40-96ELN, V40/V41-CORED8) therefore also run a V30 board:
 * without a Si5338 found at the start they behave as the V30-96ELN build (expected hardware,
 * main clock block, S/PDIF event fields and their start values, no USB audio).
 * License: CC BY-NC-SA 4.0
 */
#ifndef VARIANT_H_
#define VARIANT_H_
#include <stdint.h>
#include "flavour.h"
#include "store.h"

#define VARIANT_ELN_V30 (BAS_MODUL_SI5338 > 0 && BAS_MODUL_ES9018 > 0)

/* 1 = a 96ELN with PCB 3.0 runs this build (no Si5338 found); valid after si5338_detect_and_init */
static inline uint8_t variant_v30(void)
{
#if VARIANT_ELN_V30
	return !(hardware_other & (1u << BAS_SI5338_RDY));
#else
	return 0;
#endif
}

/* the V30-96ELN build: S/PDIF receiver events in fields 29..32 (REC 1..4), start values of
 * fields 25..28 (generated src/flavours/V30-96ELN, events_field_init_values) */
#define V30_EVENTS_FIELD_SPDIF_REC_1 29
#define V30_EXPECTED_OTHER 0x007D  /* hwcheck.c, #32 */
#endif
