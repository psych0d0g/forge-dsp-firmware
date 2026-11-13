/* libs/misc/registers + libs/misc/error_count + device part of systeminit.bas */
#ifndef REGISTERS_H_
#define REGISTERS_H_
#include <stdint.h>
#include "flavour.h"

extern uint8_t registers[BAS_MAX_REGISTERS + 1];          /* 1-based */
void registers_load(void);
void set_register(uint8_t reg, uint8_t state);
void reset_register(uint8_t reg);

extern uint8_t error_count;                               /* errors since start (< 128) */
extern uint8_t error_count_type[BAS_ERROR_COUNT_MAX + 1]; /* 1-based, 0xFF = free */
void errorcount_init(void);
void errorcount(uint8_t errortype);
void errorcount_clear(void);  /* PORT: set system.errors=0 */

/* systeminit.bas, part before the peripherals: may set sysinit and reset the MCU */
void sysinit_check(uint8_t dbg_init_finest);
void sysinit_count_boot(void);
#endif
