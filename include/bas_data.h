/*
 * BASCOM DATA tables (generated src/flavours/<flavour>/flavour_data.h): byte images in
 * flash. lookup(i, label) for homogeneous tables, restore/read as a cursor.
 */
#ifndef BAS_DATA_H_
#define BAS_DATA_H_
#include <stdint.h>
#ifdef __AVR__
#include <avr/pgmspace.h>
#define BAS_DATA_TABLE(label, n) static const uint8_t label[n] PROGMEM
static inline uint8_t bas_data_byte(const uint8_t *p) { return pgm_read_byte(p); }
#else
#define BAS_DATA_TABLE(label, n) static const uint8_t label[n] __attribute__((unused))
static inline uint8_t bas_data_byte(const uint8_t *p) { return *p; }
#endif

static inline uint16_t bas_data_word(const uint8_t *p) { return bas_data_byte(p) | (uint16_t)bas_data_byte(p + 1) << 8; }
/* lookup(i, label) on a table of words (DATA ...%) / bytes, i 0-based like BASCOM */
#define BAS_LOOKUP_W(label, i) bas_data_word(&(label)[(uint16_t)(i) * 2])
#define BAS_LOOKUP_B(label, i) bas_data_byte(&(label)[(uint16_t)(i)])

/* DATA ...# (64-bit double, requires -mdouble=64 on AVR) */
static inline double bas_data_double(const uint8_t *p)
{
	union { uint8_t b[8]; double d; } u;
	for (uint8_t i = 0; i < 8; i++)
		u.b[i] = bas_data_byte(p + i);
	return u.d;
}
#define BAS_LOOKUP_D(label, i) bas_data_double(&(label)[(uint16_t)(i) * 8])

/* restore label / read x */
typedef struct { const uint8_t *p; } bas_reader;
#define BAS_RESTORE(label) ((bas_reader){ (label) })
static inline uint8_t bas_read_b(bas_reader *r) { return bas_data_byte(r->p++); }
static inline uint16_t bas_read_w(bas_reader *r) { uint16_t v = bas_data_word(r->p); r->p += 2; return v; }
#endif
