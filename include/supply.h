#ifndef SUPPLY_H_
#define SUPPLY_H_
#include <stdint.h>
/*
 * Supply voltage monitoring (port extension, issue #46).
 *  - VCC of the MCU (3.3 V rail): internal VCC/10 on ADCB CH1 (1 V reference), always.
 *  - Supply (12-14 V) optionally on a GVOL input (X-GND-INPUT) through an external divider,
 *    linear two-point calibration [raw1,mV1,raw2,mV2] stored in the EEPROM.
 *  - Below the low threshold (debounced, 300 mV hysteresis): event system.supply=low, back
 *    above: system.supply=ok; with supply.mute on the outputs are muted while low.
 */
#define SUPPLY_HYST_MV 300
#define SUPPLY_DEBOUNCE 8          /* evaluations (events_adc_mainloop_16 cadence) */

void supply_init(void);
void supply_sample(void);           /* with events_adc_mainloop_32 */
void supply_mainloop(void);         /* with events_adc_mainloop_16 */

uint16_t supply_vcc_mv(void);
int16_t supply_ext_mv(void);        /* -1: no source configured or not calibrated */
uint16_t supply_vcc_raw(void);
uint16_t supply_gvol_raw(uint8_t n);  /* n = 1, 2 (averaged ADCA values) */
uint8_t supply_low(void);           /* 1 while below the threshold */

uint8_t supply_source(void);        /* 0 off, 1 = GVOL1, 2 = GVOL2 */
void supply_set_source(uint8_t s);
void supply_get_cal(int16_t c[4]);
uint8_t supply_set_cal(const int16_t c[4]);  /* 0: invalid (raw1 == raw2) */
uint16_t supply_low_mv(void);       /* 0 = no threshold */
void supply_set_low_mv(uint16_t mv);
uint8_t supply_mute(void);
void supply_set_mute(uint8_t on);
#endif
