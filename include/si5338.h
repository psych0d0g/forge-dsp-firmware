/* Clock generator Si5338 (libs/clock/si5338), V40/V41 */
#ifndef SI5338_H_
#define SI5338_H_
#include <stdint.h>
#include "flavour.h"
#if BAS_MODUL_SI5338 == 0
static inline void si5338_detect_and_init(void) {}
#else
void si5338_detect_and_init(void);   /* init.bas: detect @0xE0/0xE2, then si5338_init */
void si5338_load(void);
/* for tests: last calculated divider (MSx A/B/C and P1..P3) */
extern uint16_t si5338_MSx_A;
extern uint32_t si5338_MSx_B, si5338_MSx_C, si5338_MSx_P1, si5338_MSx_P2, si5338_MSx_P3;
extern uint64_t si5338_Fvco;
#endif
#endif
