/*
 * Presets in the external 24xx512 (bus 1, 0xA8): port of libs/store/cores (functions.bas).
 * Per preset: values frame storage_non_biquadvalues (mix, mux, volumes, mute, phase, delays,
 * selector, gain, CRC16) at lookup(preset, data_store_addr_values), biquads as 5 words
 * each at lookup(preset, data_store_addr_bqtv) + nr * 10.
 */
#ifndef STORE_H_
#define STORE_H_
#include <stdint.h>
#include "flavour.h"

extern uint8_t storage_non_biquadvalues[BAS_STORAGE_FRAMESIZE + 1];  /* 1-based */
extern uint8_t store_preset;        /* 0-based index into the address tables */
extern uint16_t store_bq_nr;        /* 0-based biquad number */
extern uint16_t store_chksum_error;
extern uint16_t hardware_other;

void at24c512_detect(void);         /* libs/eeprom/at24c512/init.bas */
void store_load_values_all(void);
void store_save_values_all(void);
void store_load_bqtv(void);         /* -> buffer_bytes(1..10), backwards as BASCOM */
void store_save_bqtv(void);         /* <- buffer_bytes(1..10) */
void store_load_bqtype(void);       /* -> buffer_byte: last byte of the biquad record */
uint16_t storage_chksum_get(void);
#endif
