#ifndef PGMSPACE_STUB_H_
#define PGMSPACE_STUB_H_
#include "../sim.h"
#define PROGMEM
/* Flash addresses are below SIM_FLASH_SIZE; anything else is a pointer into
 * the harness itself (ucFirmwarePattern, cast to uint32_t: needs -no-pie). */
static inline uint8_t pgm_read_byte_far(uint32_t a) {
	if (a < SIM_FLASH_SIZE) return sim_flash[a];
	return *(const uint8_t *)(uintptr_t)a;
}
#define pgm_read_byte(a) pgm_read_byte_far((uint32_t)(uintptr_t)(a))
#endif
