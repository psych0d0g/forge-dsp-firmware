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

/*
 * PORT (#69): the build for all 96ELN also runs the 96ELN with PCB 3.0, whose EEPROM has the layout
 * of the V30-96ELN build: the same up to 651, then without the 33 bytes of the Si5338 settings
 * (652..684 here), so everything from the DSP checksums on (DSP checksums, CT7302 options, IR
 * codes, event start values, control blocks) lies 33 bytes lower. On such a board (eram_v30, set
 * when no Si5338 is found) the addresses are moved, so the EEPROM stays byte for byte that of the
 * original V30 firmware. The port's own areas from 0x0F00 are not moved.
 */
extern uint8_t eram_v30;
#ifdef ERAM_SI5338_MCLK_ERAM
#define ERAM_V30_FROM ERAM_DSP_1_EEPROM_CHKSUM_ERAM
#define ERAM_V30_SHIFT (ERAM_DSP_1_EEPROM_CHKSUM_ERAM - ERAM_SI5338_MCLK_ERAM)
static inline uint16_t eram_at(uint16_t a)
{
	return eram_v30 && a >= ERAM_V30_FROM && a < ERAM_TOTAL_SIZE ? (uint16_t)(a - ERAM_V30_SHIFT) : a;
}
#else
static inline uint16_t eram_at(uint16_t a) { return a; }
#endif

static inline uint8_t eram_rb(uint16_t a) { return eram_hw_read(eram_at(a)); }
static inline uint16_t eram_rw(uint16_t a) { return eram_rb(a) | (uint16_t)eram_rb(a + 1) << 8; }
/* PORT: identical bytes are not rewritten (same content as BASCOM, less wear) */
static inline void eram_wb(uint16_t a, uint8_t v) { a = eram_at(a); if (eram_hw_read(a) != v) eram_hw_write(a, v); }
static inline void eram_ww(uint16_t a, uint16_t v) { eram_wb(a, v & 0xFF); eram_wb(a + 1, v >> 8); }
static inline uint32_t eram_rd(uint16_t a) { return eram_rw(a) | (uint32_t)eram_rw(a + 2) << 16; }
static inline void eram_wd(uint16_t a, uint32_t v) { eram_ww(a, v & 0xFFFF); eram_ww(a + 2, v >> 16); }
/* BASCOM "string * n" in ERAM: n + 1 bytes, read up to NUL, write chars + NUL */
void eram_read_str(uint16_t a, uint8_t size, char *dst);  /* dst: size bytes */
void eram_write_str(uint16_t a, uint8_t size, const char *s);
#endif
