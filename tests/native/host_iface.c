/*
 * Host harness for the protocol code (src/cmd): feeds lines from stdin into the four
 * channels and prints what is written per buart channel.
 *   input:  "<ch>|<line>"  (ch 1..4, line without CR/LF; CR LF is appended)
 *   output: one section per written buart: "@<buart>:" followed by the bytes, C-escaped
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmd.h"
#include "iface.h"
#include "mcuid.h"
#include "eram.h"
#include "registers.h"

uint8_t iface_buart_channel[iface_ch_max + 1] = { 0, 0, 2, 1, 3 };
volatile uint8_t iface_todo[iface_ch_max + 1];
char mcuid_string[17] = "ABCDEF0102030405";
static uint8_t rx[iface_ch_max + 1][128];
static int cur = -1;
int host_dtr = 1, host_ir = 0;

uint8_t iface_hook_ir_master_online(void) { return (uint8_t)host_ir; }
uint8_t iface_hook_usb_dtr(void) { return (uint8_t)host_dtr; }
void iface_hook_busy(uint8_t on) { (void)on; }
void iface_hook_watchdog(void) {}

static uint8_t eeprom[4096];
static const char *eeprom_out;
uint8_t eram_hw_read(uint16_t a) { return eeprom[a & 0xFFF]; }
void eram_hw_write(uint16_t a, uint8_t v) { eeprom[a & 0xFFF] = v; }

static void save_eeprom(void)
{
	if (!eeprom_out)
		return;
	FILE *f = fopen(eeprom_out, "wb");
	fwrite(eeprom, 1, sizeof eeprom, f);
	fclose(f);
}

void mcu_reset_delayed(void)
{
	printf("\n@reset\n");
	save_eeprom();
	exit(0);
}

uint8_t iface_copy2buffer(uint8_t ch, uint8_t *buffer)
{
	uint8_t n = (uint8_t)strlen((char *)rx[ch]);
	memcpy(buffer + 1, rx[ch], n);
	buffer[n + 1] = 0;
	rx[ch][0] = 0;
	iface_todo[ch]--;
	return n;
}

void iface_putc(uint8_t buart, char c)
{
	if (buart != cur) {
		printf("%s@%u:", cur < 0 ? "" : "\n", buart);
		cur = buart;
	}
	if (c == '\r') printf("\\r");
	else if (c == '\n') printf("\\n");
	else if (c == '\\') printf("\\\\");
	else if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) printf("\\x%02x", (unsigned char)c);
	else putchar(c);
}

void iface_flush(uint8_t buart) { (void)buart; }

/* usage: cmd-<flavour> [EEPROM_IN|- [EEPROM_OUT]]  (default: blank 0xFF, i.e. sysinit) */
int main(int argc, char **argv)
{
	char line[512];
	memset(eeprom, 0xFF, sizeof eeprom);
	if (argc > 1 && strcmp(argv[1], "-")) {
		FILE *f = fopen(argv[1], "rb");
		if (!f || fread(eeprom, 1, sizeof eeprom, f) == 0) { perror(argv[1]); return 2; }
		fclose(f);
	}
	if (argc > 2)
		eeprom_out = argv[2];
	errorcount_init();
	registers_load();
	sysinit_check(0);
	sysinit_count_boot();
	cmd_init();
	while (fgets(line, sizeof line, stdin)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!strncmp(line, "#dtr=", 5)) { host_dtr = atoi(line + 5); continue; }
		if (!strncmp(line, "#ir=", 4)) { host_ir = atoi(line + 4); continue; }
		int ch = line[0] - '0';
		if (ch < 1 || ch > 4 || line[1] != '|')
			continue;
		snprintf((char *)rx[ch], sizeof rx[ch], "%.125s\r\n", line + 2);
		iface_todo[ch] = 1;
		cmd_check4buffer();
		if (cur >= 0) { printf("\n"); cur = -1; }
		printf("--\n");
	}
	save_eeprom();
	return 0;
}
