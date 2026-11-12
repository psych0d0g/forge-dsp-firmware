/*
 * TAS3108 DSPs (libs/dsp/tas3108): program EEPROMs (at24c512/1..3) and core detection
 * (cores/1..3 init). Core 1: 0x68 bus 1, EEPROM 0xA0 bus 1, dsp_1_code;
 * core 2: 0x68 bus 2, EEPROM 0xA0 bus 2, dsp_2_code; core 3: 0x6A bus 2, EEPROM 0xA2 bus 2,
 * dsp_2_code ("DSP 2&3 use the same schema").
 */
#ifndef TAS3108_H_
#define TAS3108_H_
#include <stdint.h>
extern uint16_t hardware_dsp;
void dsp_eeprom_init(uint8_t n);     /* at24c512/<n>/init.bas: detect, compare, rewrite if needed */
void dsp_core_init(uint8_t n);       /* cores/<n>/init.bas: detect, version, state */
/* PORT (#47): a core reported an error state during this start (e.g. it did not load its program) */
extern uint8_t dsp_state_error;
#endif
