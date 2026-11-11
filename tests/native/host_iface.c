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
#include "i2c.h"
#include "store.h"
#include "expander.h"
#include "si5338.h"
#include "tas3108.h"
#include "dac.h"
#include "ct7302.h"
#include "flavour_systeminit.h"
#include "biquad.h"
void interrupts_enable(void) { printf("@sei\n"); }
void watchdog_start(void) {}
void watchdog_kick(void) {}
struct ee { uint8_t bus, addr; uint8_t *mem; uint32_t writes; };
extern struct ee ees[4];
extern uint8_t dspee[3][65536];
static const char *dspee_out[3];
void host_i2c_add(uint8_t bus, uint8_t addr);
uint8_t *host_i2c_regs(uint8_t bus, uint8_t addr);

uint8_t iface_buart_channel[iface_ch_max + 1] = { 0, 0, 2, 1, 3 };
volatile uint8_t iface_todo[iface_ch_max + 1];
char mcuid_string[17] = "ABCDEF0102030405";
static uint8_t rx[iface_ch_max + 1][128];
static int cur = -1;
int host_dtr = 1, host_ir = 0;
extern int host_i2c_log;
extern uint8_t at24[65536];
static const char *at24_out;

static void store_cmd(const char *l)
{
	if (!strncmp(l, "#at24=", 6)) {
		FILE *f = fopen(l + 6, "rb");
		if (!f || fread(at24, 1, sizeof at24, f) != sizeof at24) { perror(l + 6); exit(2); }
		fclose(f);
	} else if (!strncmp(l, "#at24out=", 9)) {
		at24_out = strdup(l + 9);
	} else if (!strncmp(l, "#i2cdev=", 8)) {  /* #i2cdev=<bus>,<addr hex>[,<reg>=<val>...] */
		unsigned bus, addr;
		sscanf(l + 8, "%u,%x", &bus, &addr);
		host_i2c_add((uint8_t)bus, (uint8_t)addr);
		const char *q = strchr(strchr(l, ',') + 1, ',');
		while (q) {
			unsigned r, v;
			if (sscanf(q + 1, "%x=%x", &r, &v) == 2)
				host_i2c_regs((uint8_t)bus, (uint8_t)addr)[r] = (uint8_t)v;
			q = strchr(q + 1, ',');
		}
	} else if (!strncmp(l, "#regs=", 6)) {      /* #regs=<bus>,<addr hex>,<n> */
		unsigned bus, addr, n;
		sscanf(l + 6, "%u,%x,%u", &bus, &addr, &n);
		uint8_t *r = host_i2c_regs((uint8_t)bus, (uint8_t)addr);
		printf("@regs:");
		for (unsigned i = 0; i < n; i++)
			printf("%02X", r ? r[i] : 0);
		printf("\n");
	} else if (!strncmp(l, "#dspee=", 7)) {      /* #dspee=<1..3>,<file> */
		int n = l[7] - '0';
		FILE *f = fopen(l + 9, "rb");
		if (!f || fread(dspee[n - 1], 1, 65536, f) != 65536) { perror(l + 9); exit(2); }
		fclose(f);
	} else if (!strncmp(l, "#dspee_out=", 11)) {
		dspee_out[l[11] - '1'] = strdup(l + 13);
	} else if (!strncmp(l, "#dsp_eeprom=", 12)) {  /* run at24c512/<n>/init */
		int n = atoi(l + 12);
		uint32_t before = ees[n == 1 ? 1 : n].writes;
		dsp_eeprom_init((uint8_t)n);
		printf("@dsp_eeprom%d: hw=%04X writes=%u\n", n, hardware_dsp, ees[n == 1 ? 1 : n].writes - before);
	} else if (!strncmp(l, "#dsp_core=", 10)) {
		dsp_core_init((uint8_t)atoi(l + 10));
		printf("@dsp_core: hw=%04X\n", hardware_dsp);
	} else if (!strncmp(l, "#bq_calc=", 9)) {  /* type,f,gain(0.1dB),q(1/1000),b,flags as integers */
		int t, f, g, q, b, fl;
		sscanf(l + 9, "%d,%d,%d,%d,%d,%d", &t, &f, &g, &q, &b, &fl);
		dsp_bq_cache_type = (uint8_t)t;
		if (!(fl & 0x80))
			dsp_bq_cache_type = 0;  /* flags bit 7 = 0: all pass */
		dsp_bq_cache_values[1] = f;
		dsp_bq_cache_values[3] = g; dsp_bq_cache_values[3] = dsp_bq_cache_values[3] / 10;
		dsp_bq_cache_values[2] = q; dsp_bq_cache_values[2] = dsp_bq_cache_values[2] / 1000;
		dsp_bq_cache_values[4] = b;
		dsp_bq_calc_type(dsp_bq_cache_type);
		dsp_bq_calcto5coeff();
		uint32_t raw[6];
		dsp_bq_convert2raw(raw);
		printf("@bq:%08X %08X %08X %08X %08X\n", raw[1], raw[2], raw[3], raw[4], raw[5]);
	} else if (!strcmp(l, "#systeminit")) {
		flavour_systeminit_peripherals();
		printf("@hw:%04X dsp:%04X\n", hardware_other, hardware_dsp);
	} else if (!strcmp(l, "#spdif_init")) {
		spdif_init();
		printf("@hw:%04X\n", hardware_other);
#if BAS_MODUL_CT7302 > 0
	} else if (!strncmp(l, "#ct7302_mux=", 12)) {
		ct7302_20_set_mux((uint8_t)atoi(l + 12));
		printf("@mux:%u\n", ct7302_20_get_mux());
#endif
	} else if (!strcmp(l, "#dac_init")) {
		dac_init();
		printf("@hw:%04X\n", hardware_other);
	} else if (!strcmp(l, "#si5338")) {
		si5338_detect_and_init();
		printf("@hw:%04X\n", hardware_other);
	} else if (!strcmp(l, "#expander_init")) {
		expander_init();
		printf("@hw:%04X\n", hardware_other);
	} else if (!strncmp(l, "#pca9555=", 9)) {   /* #pca9555=<bits hex>,<value hex> */
		unsigned b, v;
		sscanf(l + 9, "%x,%x", &b, &v);
		pca9555_bits = (uint16_t)b;
		pca9555_value = (uint16_t)v;
		pca9555_4F_set_output();
	} else if (!strncmp(l, "#pca9554=", 9)) {
		unsigned b, v;
		sscanf(l + 9, "%x,%x", &b, &v);
		pca9554_bits = (uint8_t)b;
		pca9554_value = (uint8_t)v;
		pca9554_4F_set_output();
	} else if (!strncmp(l, "#i2clog=", 8)) {
		host_i2c_log = atoi(l + 8);
	} else if (!strncmp(l, "#store_load=", 12)) {
		store_preset = (uint8_t)atoi(l + 12);
		uint16_t before = store_chksum_error;
		store_load_values_all();
		printf("@store:");
		for (int i = 1; i <= BAS_STORAGE_FRAMESIZE; i++)
			printf("%02X", storage_non_biquadvalues[i]);
		printf(" chksum_error=%u\n", store_chksum_error - before);
	} else if (!strncmp(l, "#store_save=", 12)) {
		store_preset = (uint8_t)atoi(l + 12);
		store_save_values_all();
	} else if (!strncmp(l, "#store_set=", 11)) {  /* #store_set=<index>,<byte> (1-based) */
		int i, v;
		sscanf(l + 11, "%d,%d", &i, &v);
		storage_non_biquadvalues[i] = (uint8_t)v;
	} else if (!strncmp(l, "#bq=", 4)) {
		int pr, nr;
		sscanf(l + 4, "%d,%d", &pr, &nr);
		store_preset = (uint8_t)pr;
		store_bq_nr = (uint16_t)nr;
		store_load_bqtv();
		printf("@bq:");
		for (int i = 1; i <= BAS_STORAGE_BQTV_VALUES_FRAMESIZE; i++)
			printf("%02X", buffer_bytes[i]);
		store_load_bqtype();
		printf(" type=%02X\n", buffer_byte);
	}
	printf("--\n");
}

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
	memset(at24, 0xFF, sizeof at24);
	memset(dspee, 0xFF, sizeof dspee);
	i2c_bus_init(1);
	i2c_bus_init(2);
	at24c512_detect();
	cmd_init();
	while (fgets(line, sizeof line, stdin)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!strncmp(line, "#dtr=", 5)) { host_dtr = atoi(line + 5); continue; }
		if (!strncmp(line, "#ir=", 4)) { host_ir = atoi(line + 4); continue; }
		if (line[0] == '#') { store_cmd(line); continue; }
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
	for (int i = 0; i < 3; i++)
		if (dspee_out[i]) {
			FILE *f = fopen(dspee_out[i], "wb");
			fwrite(dspee[i], 1, 65536, f);
			fclose(f);
		}
	if (at24_out) {
		FILE *f = fopen(at24_out, "wb");
		fwrite(at24, 1, sizeof at24, f);
		fclose(f);
	}
	return 0;
}
