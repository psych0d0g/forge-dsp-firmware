/* DACs (libs/da): ES90x8Pro (96ELN-3), ES9018 (96ELN, CORED8, V30-96ELN), CS4385 (V30-96LN) */
#ifndef DAC_H_
#define DAC_H_
#include <stdint.h>
#include "flavour.h"
void dac_init(void);                 /* libs/da/init.bas of the fitted DAC */
#if BAS_MODUL_CS4385 > 0
extern uint8_t cs4385_reg, cs4385_value;
void cs4385_set_reg(void);
void cs4385_set_mute(uint8_t mask);
#endif
/* bit n = channel n+1 muted (volume 0xFF), else 0 dB; 100/130 ms ramp per channel */
void es90x8pro_set_mute(uint8_t mask);
void es9018_set_mute(uint8_t mask);
void es9018_set_phase(uint8_t v);
#endif
