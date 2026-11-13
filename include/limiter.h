/*
 * PORT extension (#58): output limiter in the DSP (cores 3 and 2, tools/dsp_limiter.py).
 * Thresholds per output in tenths of dBFS (0 = off, down to -240), attack in tenths of ms,
 * release in ms; stored in the EEPROM (0x0F30, per device, not per preset) and written to the
 * DSP at the start and on every change.
 */
#ifndef LIMITER_H_
#define LIMITER_H_
#include <stdint.h>
#define LIMITER_MIN_TENTHS (-240)
uint8_t limiter_available(void);         /* the DSP program has the block */
int16_t limiter_threshold(uint8_t out);  /* out 1..8, tenths of dB, 0 = off */
uint16_t limiter_attack(void);           /* tenths of ms */
uint16_t limiter_release(void);          /* ms */
uint8_t limiter_set_threshold(uint8_t out, int16_t tenths);  /* 0 = out of range */
uint8_t limiter_set_attack(uint16_t tenths_ms);
uint8_t limiter_set_release(uint16_t ms);
void limiter_apply(void);                /* all coefficients to the DSP (after the cores are up) */
void cmd_get_system_dsp(void);
void cmd_set_system_dsp(void);
#endif
