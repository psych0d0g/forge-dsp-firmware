/*
 * Factory reset ("sysinit") of the original: after a blank or foreign EEPROM, a firmware with
 * another version string, system.reinit or system.hardware.memory.eram the modules write their
 * defaults (internal EEPROM, preset EEPROM erased and preset 0 copied to all presets), then
 * startup_state = done and a reset.
 * FORGE_FACTORY_RESET=0 (default): the port only uses the defaults in RAM and keeps
 * startup_state at sysinit, nothing is erased. 1: as the original (deletes all tuning).
 */
#ifndef FACTORY_H_
#define FACTORY_H_
#include <stdint.h>
#include "flavour.h"
#include "registers.h"
#ifndef FORGE_FACTORY_RESET
#define FORGE_FACTORY_RESET 0
#endif
extern uint8_t factory_reset_enabled;  /* FORGE_FACTORY_RESET; the native tests switch it */
static inline uint8_t sysinit_active(void)
{
	return registers[BAS_REG_STARTUP_STATE] == BAS_STARTUP_STATE_SYSINIT;
}
/* the module writes its defaults now */
static inline uint8_t factory_write(void) { return factory_reset_enabled && sysinit_active(); }
#endif
