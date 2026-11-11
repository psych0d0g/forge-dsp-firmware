/* ADCA (2 pots, signed 12 bit, VCC/1.6) and ADCB (temperature, unsigned, 1 V) as
 * libs/ctrl/events/adc/init.bas; getadc() of BASCOM. Native tests provide their own. */
#ifndef ADC_HW_H_
#define ADC_HW_H_
#include <stdint.h>
void adc_hw_init(void);
uint16_t adc_hw_get(uint8_t adc, uint8_t ch);   /* adc 0 = ADCA, 1 = ADCB */
uint16_t adc_hw_tempsense(void);                /* signature row TEMPSENSE0/1 (85 °C reading) */
#endif
