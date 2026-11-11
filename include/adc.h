/* ADCs (libs/ad): PCM4202 is pin controlled (no init); CS5366 on V30-96LN */
#ifndef ADC_H_
#define ADC_H_
#include <stdint.h>
#include "flavour.h"
#if BAS_MODUL_CS5366 > 0
void adc_init(void);
void cs5366_set_mute(uint8_t v);
void cs5366_set_highpass(uint8_t v);
void cs5366_reset_overflow(void);
#else
static inline void adc_init(void) {}
#endif
#endif
