/* DACs (libs/da): ES90x8Pro (96ELN-3), ES9018 (96ELN, CORED8, V30-96ELN), CS4385 (V30-96LN) */
#ifndef DAC_H_
#define DAC_H_
#include <stdint.h>
#include "flavour.h"
#if BAS_MODUL_ES90X8PRO > 0 || BAS_MODUL_ES9018 > 0
void dac_init(void);                 /* libs/da/init.bas of the fitted DAC */
#else
static inline void dac_init(void) {} /* PORT: CS4385 (V30-96LN) follows */
#endif
/* bit n = channel n+1 muted (volume 0xFF), else 0 dB; 100/130 ms ramp per channel */
void es90x8pro_set_mute(uint8_t mask);
void es9018_set_mute(uint8_t mask);
void es9018_set_phase(uint8_t v);
#endif
