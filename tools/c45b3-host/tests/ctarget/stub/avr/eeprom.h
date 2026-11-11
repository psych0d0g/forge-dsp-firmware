#ifndef EEPROM_STUB_H_
#define EEPROM_STUB_H_
#include "../sim.h"
#define eeprom_busy_wait() ((void)0)
static inline void eeprom_write_block(const void *src, void *dst, size_t n) {
	uintptr_t a = (uintptr_t)dst;
	if (a + n > E2END + 1) { fprintf(stderr, "FAULT eeprom write beyond E2END\n"); abort(); }
	memcpy(sim_eeprom + a, src, n);
}
static inline uint8_t eeprom_read_byte(const uint8_t *p) {
	return sim_eeprom[(uintptr_t)p & E2END];
}
#endif
