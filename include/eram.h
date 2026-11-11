/*
 * Internal EEPROM (BASCOM ERAM). Addresses: src/flavours/<flavour>/flavour_eram.h.
 * BASCOM arrays are 1-based: element i of X is at ERAM_X + (i - 1) * ERAM_X_SIZE.
 */
#ifndef ERAM_H_
#define ERAM_H_
#include <stdint.h>
#include "flavour_eram.h"

#define ERAM_EL(var, i) ((uint16_t)(ERAM_##var + ((uint16_t)(i) - 1) * ERAM_##var##_SIZE))

/* backend: AVR = memory-mapped read + NVM page write, host tests = array */
uint8_t eram_hw_read(uint16_t a);
void eram_hw_write(uint16_t a, uint8_t v);

static inline uint8_t eram_rb(uint16_t a) { return eram_hw_read(a); }
static inline uint16_t eram_rw(uint16_t a) { return eram_hw_read(a) | (uint16_t)eram_hw_read(a + 1) << 8; }
/* PORT: identical bytes are not rewritten (same content as BASCOM, less wear) */
static inline void eram_wb(uint16_t a, uint8_t v) { if (eram_hw_read(a) != v) eram_hw_write(a, v); }
static inline void eram_ww(uint16_t a, uint16_t v) { eram_wb(a, v & 0xFF); eram_wb(a + 1, v >> 8); }
/* BASCOM "string * n" in ERAM: n + 1 bytes, read up to NUL, write chars + NUL */
void eram_read_str(uint16_t a, uint8_t size, char *dst);  /* dst: size bytes */
void eram_write_str(uint16_t a, uint8_t size, const char *s);
#endif
