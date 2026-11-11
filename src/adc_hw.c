/*
 * ADC setup of libs/ctrl/events/adc/init.bas (mcu_a3_v30 / mcu_a3_v40, identical):
 *   ADCA: signed, 12 bit, INTVCC, prescaler 512, CH0/CH1 single ended on pins_ad_mux_vol1/2
 *   ADCB: unsigned, 12 bit, INT1V, prescaler 512, temperature reference, CH0 internal (temp)
 * Calibration from the signature row (Readsig &H20/&H21, &H24/&H25).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#ifdef __AVR__
#include <avr/io.h>
#include <stddef.h>
#include "adc_hw.h"
#include "flavour.h"

static uint8_t readsig(uint8_t offset)
{
	NVM.CMD = NVM_CMD_READ_CALIB_ROW_gc;
	uint8_t v;
	__asm__ volatile("lpm %0, Z" : "=r"(v) : "z"((uint16_t)offset));
	NVM.CMD = NVM_CMD_NO_OPERATION_gc;
	return v;
}

uint16_t adc_hw_tempsense(void) { return (uint16_t)(readsig(0x2E) | readsig(0x2F) << 8); }

void adc_hw_init(void)
{
	ADCA.CALL = readsig(0x20);
	ADCA.CALH = readsig(0x21);
	ADCB.CALL = readsig(0x24);
	ADCB.CALH = readsig(0x25);
	ADCA.CTRLB = ADC_CONMODE_bm | ADC_RESOLUTION_12BIT_gc;
	ADCA.REFCTRL = ADC_REFSEL_VCC_gc;
	ADCA.PRESCALER = ADC_PRESCALER_DIV512_gc;
	ADCA.CH0.CTRL = ADC_CH_GAIN_1X_gc | ADC_CH_INPUTMODE_SINGLEENDED_gc;
	ADCA.CH0.MUXCTRL = BAS_PINS_AD_MUX_VOL1;
	ADCA.CH1.CTRL = ADC_CH_GAIN_1X_gc | ADC_CH_INPUTMODE_SINGLEENDED_gc;
	ADCA.CH1.MUXCTRL = BAS_PINS_AD_MUX_VOL2;
	ADCB.CTRLB = ADC_RESOLUTION_12BIT_gc;
	ADCB.REFCTRL = ADC_REFSEL_INT1V_gc | ADC_TEMPREF_bm;
	ADCB.PRESCALER = ADC_PRESCALER_DIV512_gc;
	ADCB.CH0.CTRL = ADC_CH_GAIN_1X_gc | ADC_CH_INPUTMODE_INTERNAL_gc;
	ADCB.CH0.MUXCTRL = ADC_CH_MUXINT_TEMP_gc;
	ADCA.CTRLA = ADC_ENABLE_bm;  /* start ADCA */
	ADCB.CTRLA = ADC_ENABLE_bm;  /* start ADCB */
}

uint16_t adc_hw_get(uint8_t adc, uint8_t ch)
{
	ADC_t *a = adc ? &ADCB : &ADCA;
	ADC_CH_t *c = ch ? &a->CH1 : &a->CH0;
	c->INTFLAGS = ADC_CH_CHIF_bm;
	c->CTRL |= ADC_CH_START_bm;
	while (!(c->INTFLAGS & ADC_CH_CHIF_bm))
		;
	c->INTFLAGS = ADC_CH_CHIF_bm;
	return c->RES;
}
#endif
