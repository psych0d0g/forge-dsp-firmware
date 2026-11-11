/*
 * Text protocol, milestone M0: only what is needed to identify the firmware and
 * to jump into the bootloader. Line format as libs/cmd/v20/commands.txt:
 *   <command>(.<target>) <register>=<value>(;<chksum>) CR LF, max. 127 chars
 * Answers follow cmd_answer_header (" <.2 system.firmware=...").
 *
 * New, not in the BASCOM firmware:
 *   <system.key.info      as BASCOM (used by the upload to check model and PCB)
 *   >system.bootloader=1   answer " *.2 system.bootloader ok", then software reset
 *                          into chip45boot3 (RXD high -> autobaud for ~3 s).
 * License: CC BY-NC-SA 4.0 (protocol of the Forge DSP firmware, Mark Duprau).
 */
#include <string.h>
#include <util/delay.h>
#include "cmd.h"
#include "flavour.h"
#include "mcuid.h"
#include "power.h"
#include "uart.h"
#include "version.h"

#define LINE_MAX 127
static char line[LINE_MAX + 1];
static uint8_t len;
static uint8_t overflow;

static void answer(char type, const char *reg, const char *rest)
{
	uart_putc(' ');
	uart_putc(type);
	uart_puts(".2 ");  /* CMD_OWN_RECEIVER */
	uart_puts(reg);
	uart_puts(rest);
	uart_puts("\r\n");
}

static void handle(char *s)
{
	char type = s[0];
	char *p = s + 1;
	if (*p == '.') {             /* target number: ignored in M0 (no RS485 chain yet) */
		while (*p && *p != ' ')
			p++;
	}
	while (*p == ' ')
		p++;
	char *semi = strchr(p, ';');  /* optional checksum: not checked in M0 */
	if (semi)
		*semi = 0;

	if (type == '<' && strcmp(p, "system.firmware") == 0) {
		answer('<', "system.firmware", "=\"" FW_VERSION_STR "\"");
	} else if (type == '<' && strcmp(p, "system.key.info") == 0) {
		/* cmd_get_system_key_info: vendor-model-board-mcuid-CCBYNCSA-date-MDD */
		uart_puts(" <.2 system.key.info=\"" BAS_VENDOR_ID "-" FLAVOUR_MODEL "-" FLAVOUR_BOARD "-");
		uart_puts(mcuid_string);
		uart_puts("-CCBYNCSA-" FW_BUILD_DATE "-MDD\"\r\n");
	} else if (type == '>' && strcmp(p, "system.bootloader=1") == 0) {
		answer('*', "system.bootloader", " ok");
		uart_flush();
		_delay_ms(10);
		mcu_reset();
	} else if (type == '<' || type == '>' || type == '?') {
		answer('!', p, " unknown command");
	}
	/* events/infos/errors from other devices are ignored */
}

void cmd_poll(void)
{
	int c;
	while ((c = uart_getc()) >= 0) {
		if (c == '\r' || c == '\n') {
			if (len && !overflow) {
				line[len] = 0;
				handle(line);
			}
			len = 0;
			overflow = 0;
		} else if (len < LINE_MAX) {
			line[len++] = (char)c;
		} else {
			overflow = 1;
		}
	}
}
