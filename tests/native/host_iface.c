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

void mcu_reset_delayed(void)
{
	printf("\n@reset\n");
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

int main(void)
{
	char line[512];
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
	return 0;
}
