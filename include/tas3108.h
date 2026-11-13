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

/* PORT diagnostics (#57): what each core did during a start. dsp_boot_now is this start;
 * dsp_boot_prev keeps the start before a restart of hw_check_dsp_restart across the software
 * reset (not initialised, valid with DSP_BOOT_MAGIC, cleared at power-on). */
#include "i2c.h"
#define DSP_BOOT_MAGIC 0x57
struct dsp_boot_core {
	uint8_t written;     /* EEPROM rewritten this start: 0 no, 1 yes, 2 twice (verify failed once) */
	uint8_t verify;      /* read back after writing: 0 not written, 1 sum ok, 2 wrong */
	uint8_t version[4];  /* subaddress 0x08, buffer_bytes(1..4) */
	uint8_t state[4];    /* subaddress 0x02 */
	uint8_t found;       /* answered on I2C */
};
struct dsp_boot_rec {
	uint8_t magic, check;  /* check: byte sum of the rest, against garbage after power-on */
	struct dsp_boot_core core[3];
	struct dspboot_diag bus[3];
};
extern struct dsp_boot_rec dsp_boot_now, dsp_boot_prev;
void dsp_boot_keep(void);       /* before the restart: dsp_boot_now -> dsp_boot_prev */
void dsp_boot_power_on(void);   /* at a power-on reset: dsp_boot_prev is garbage */
uint8_t dsp_boot_prev_valid(void);
#endif
