/*
 * Port of libs/sys/init (sys_init_sets) and libs/ctrl/events/vumeter. Both are unfinished in
 * the original and reproduced as such:
 * - sys_init: up to 8 command strings in the EEPROM are counted at the start; the main loop
 *   copies always set 1 into cmd_buffer and decrements the counter, but nothing parses that
 *   buffer afterwards, and no command writes the sets (issue in the tracker). The port only
 *   counts down.
 * - VU meter: the body of events_vumeter_mainloop is commented out in the original.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "eram.h"
#include "flavour.h"
#include "registers.h"
#include "factory.h"

static uint8_t sys_init_sets_exe_count, sys_init_sets_exe_state;

void sys_init_init(void)
{
	sys_init_sets_exe_count = 0;
	sys_init_sets_exe_state = 0;
	if (factory_write())  /* sets cleared ("") */
		for (uint8_t i = 1; i <= ERAM_SYS_INIT_SETS_ERAM_COUNT; i++)
			eram_write_str(ERAM_EL(SYS_INIT_SETS_ERAM, i), ERAM_SYS_INIT_SETS_ERAM_SIZE, "");
	if (sysinit_active())
		return;
	for (uint8_t i = 1; i <= ERAM_SYS_INIT_SETS_ERAM_COUNT; i++) {
		uint8_t c = eram_rb(ERAM_EL(SYS_INIT_SETS_ERAM, i));
		if (c != 0x00 && c != 0xFF) {  /* len(...) > 0; 0xFF = never written */
			sys_init_sets_exe_count++;
			sys_init_sets_exe_state |= (uint8_t)(1u << (i & 7));
		}
	}
}

void sys_init_mainloop(void)
{
	if (sys_init_sets_exe_count > 0) {
		sys_init_sets_exe_state &= (uint8_t)~(1u << 1);  /* sys_init_execute_next_set: always set 1 */
		sys_init_sets_exe_count--;
	}
}

void events_vumeter_init(void) {}
void events_vumeter_mainloop(void) {}
