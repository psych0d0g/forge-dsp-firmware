/*
 * Port of libs/sys/init (sys_init_sets) and libs/ctrl/events/vumeter. Both are unfinished in
 * the original and reproduced as such:
 * - sys_init: up to 8 command strings in the EEPROM. The original counts them at the start
 *   and copies always set 1 into cmd_buffer, but nothing parses it and no command writes the
 *   sets. PORT extension (#40): system.startup.N sets them, they run after the first preset
 *   load.
 * - VU meter: the body of events_vumeter_mainloop is commented out in the original.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "eram.h"
#include "flavour.h"
#include "registers.h"
#include "factory.h"
#include <string.h>
#include "cmd.h"
#include "iface.h"
#include "sys_init.h"

static uint8_t next_set;  /* 1..8: next start command to run, 0 = idle */

void sys_init_init(void)
{
	next_set = 0;
	if (factory_write())  /* sets cleared ("") */
		for (uint8_t i = 1; i <= ERAM_SYS_INIT_SETS_ERAM_COUNT; i++)
			eram_write_str(ERAM_EL(SYS_INIT_SETS_ERAM, i), ERAM_SYS_INIT_SETS_ERAM_SIZE, "");
}

/* PORT (#40): the original counts the sets but never runs them. The port runs them once after
 * the first preset load of a start (so they are not overwritten by it), one per pass. */
void sys_init_preset_loaded(void)
{
	static uint8_t done;
	if (!done && !sysinit_active()) {
		done = 1;
		next_set = 1;
	}
}

uint8_t sys_init_get(uint8_t i, char *out)
{
	eram_read_str(ERAM_EL(SYS_INIT_SETS_ERAM, i), ERAM_SYS_INIT_SETS_ERAM_SIZE, out);
	if ((uint8_t)out[0] == 0xFF)
		out[0] = 0;  /* never written */
	return (uint8_t)strlen(out);
}

void sys_init_mainloop(void)
{
	while (next_set >= 1 && next_set <= ERAM_SYS_INIT_SETS_ERAM_COUNT) {
		char line[ERAM_SYS_INIT_SETS_ERAM_SIZE + 1];
		uint8_t i = next_set++;
		if (sys_init_get(i, line) == 0)
			continue;
		for (char *c = line; *c; c++)
			if (*c == ':')
				*c = '=';  /* stored with ':' because the parser keeps only the text after the last '=' */
		uint8_t ch = buart_channel;
		buart_channel = iface_buart_channel[iface_ch_usb];
		cmd_run_line(line);
		buart_channel = ch;
		return;  /* one command per pass */
	}
	next_set = 0;
}

void events_vumeter_init(void) {}
void events_vumeter_mainloop(void) {}
