#include <math.h>
/*
 * Host harness for the protocol code (src/cmd): feeds lines from stdin into the four
 * channels and prints what is written per buart channel.
 *   input:  "<ch>|<line>"  (ch 1..4, line without CR/LF; CR LF is appended)
 *   output: one section per written buart: "@<buart>:" followed by the bytes, C-escaped
 */
#include "supply.h"
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
#include "dsp.h"
void interrupts_enable(void) { printf("@sei\n"); }
void watchdog_start(void) {}
void watchdog_kick(void) {}
struct ee { uint8_t bus, addr; uint8_t *mem; uint32_t writes; };
extern struct ee ees[4];
extern uint8_t dspee[3][65536];
static const char *dspee_out[3];
void host_i2c_add(uint8_t bus, uint8_t addr);
uint8_t *host_i2c_regs(uint8_t bus, uint8_t addr);
void host_i2c_readonly(uint8_t bus, uint8_t addr);
void host_i2c_block(uint8_t bus, uint8_t addr);
#include "events.h"
#include "modc.h"
#include "adc_hw.h"
#include "tick.h"
#include "ir.h"
#include "sd.h"
#include "log.h"
#include "ff.h"
void host_sd_card(int on);
int host_sd_image(const char *path, unsigned mb);
int host_sd_put(const char *name, const char *hostfile, int y, int mo, int d, int h, int mi);
/* #irbuf=<pulse/space list>: fill the IR sample buffer as TCC1 would (0 = IR active) */
static void host_ir_fill(const char *spec)
{
	uint32_t pos = 0;
	for (int i = 1; i <= IR_MAX_BUFFER; i++)
		irreceiver_buffer[i] = 0xFFFFFFFFUL;
	int level = 0;
	const char *p = spec;
	while (*p) {
		long n = strtol(p, (char **)&p, 10);
		for (long k = 0; k < n && pos < IR_MAX_BUFFER * 32UL; k++, pos++) {
			uint32_t m = 1UL << (pos % 32);
			if (level) irreceiver_buffer[1 + pos / 32] |= m; else irreceiver_buffer[1 + pos / 32] &= ~m;
		}
		level ^= 1;
		if (*p == ',') p++;
	}
	irreceiver_state |= 1;  /* buffer used */
}
volatile uint16_t timer_tce0_counter;
static uint16_t host_ms;  /* PORT (#60): time of the log */
uint16_t tick_ms(void) { return host_ms; }
volatile int16_t timer_tce0_value;
volatile uint8_t timer_tce0_factor = 1, timer_tce0_min = 0, timer_tce0_max = 255;
static uint16_t host_adc[2][2], host_tempsense = 0x0A00;
void adc_hw_init(void) {}
uint16_t adc_hw_get(uint8_t adc, uint8_t ch) { return host_adc[adc & 1][ch & 1]; }
uint16_t adc_hw_tempsense(void) { return host_tempsense; }
void power_shutdown(uint8_t a, uint8_t b) { printf("@shutdown:%u,%u\n", a, b); }
void mcu_reset(void) { printf("@soft_reset\n"); }
static struct { char name[40]; int v; } pins[32];
int host_pin_read(const char *name)
{
	for (int i = 0; i < 32; i++)
		if (!strcmp(pins[i].name, name))
			return pins[i].v;
	return 0;
}
static void host_pin_set(const char *name, int v)
{
	for (int i = 0; i < 32; i++)
		if (!pins[i].name[0] || !strcmp(pins[i].name, name)) {
			snprintf(pins[i].name, sizeof pins[i].name, "%s", name);
			pins[i].v = v;
			return;
		}
}
const uint8_t *host_i2c_blockdata(uint8_t bus, uint8_t addr, uint8_t reg);
#include "api.h"
#include "hwcheck.h"
#include "bqramp.h"
#include "level.h"

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
	} else if (!strncmp(l, "#i2cnack=", 9)) {  /* #i2cnack=<bus>,<addr hex>,<n>: NACK the next n addressings */
		unsigned bus, addr, n;
		void host_i2c_nack(uint8_t bus, uint8_t addr, uint8_t n);
		sscanf(l + 9, "%u,%x,%u", &bus, &addr, &n);
		host_i2c_nack((uint8_t)bus, (uint8_t)addr, (uint8_t)n);
	} else if (!strcmp(l, "#dsp_restart")) {     /* #45: end of systeminit, one restart if cores are missing */
		hw_check_dsp_restart();
		printf("@dsp_restart: hw=%04X flag=%u restarted=%u\n", hardware_dsp, eram_hw_read(0x0F11), hw_dsp_restarted);
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
	} else if (!strncmp(l, "#dsp_bq=", 8)) {   /* #dsp_bq=<virt nr>,type,f,gain,q,b,flags */
		int nr, t, f, g, q, b, fl;
		sscanf(l + 8, "%d,%d,%d,%d,%d,%d,%d", &nr, &t, &f, &g, &q, &b, &fl);
		dsp_bq_cache_values[1] = f;
		dsp_bq_cache_values[3] = g; dsp_bq_cache_values[3] = dsp_bq_cache_values[3] / 10;
		dsp_bq_cache_values[2] = q; dsp_bq_cache_values[2] = dsp_bq_cache_values[2] / 1000;
		dsp_bq_cache_values[4] = b;
		dsp_bq_calc_type((fl & 0x80) ? (uint8_t)t : 0);
		dsp_bq_calcto5coeff();
		uint32_t raw[6];
		dsp_bq_convert2raw(raw);
		dsp_bq_virt_nr = (uint16_t)nr;
		dsp_calc_core();
		dsp_calc_reg();
		dsp_set_biquad(raw);
		printf("@dsp: core=%u reg=%02X verify_errors=%u\n", dsp_core, dsp_reg, iface_verify_error_count);
	} else if (!strncmp(l, "#i2cro=", 7)) {      /* #i2cro=<bus>,<addr hex>: writes are ignored */
		unsigned bus, addr;
		sscanf(l + 7, "%u,%x", &bus, &addr);
		host_i2c_readonly((uint8_t)bus, (uint8_t)addr);
	} else if (!strncmp(l, "#i2cdsp=", 8)) {     /* #i2cdsp=<bus>,<addr hex>: TAS3108 block registers */
		unsigned bus, addr;
		sscanf(l + 8, "%u,%x", &bus, &addr);
		host_i2c_add((uint8_t)bus, (uint8_t)addr);
		host_i2c_block((uint8_t)bus, (uint8_t)addr);
	} else if (!strncmp(l, "#block=", 7)) {      /* #block=<bus>,<addr hex>,<reg hex>,<n> */
		unsigned bus, addr, reg, n;
		sscanf(l + 7, "%u,%x,%x,%u", &bus, &addr, &reg, &n);
		const uint8_t *b = host_i2c_blockdata((uint8_t)bus, (uint8_t)addr, (uint8_t)reg);
		printf("@block:");
		for (unsigned i = 0; i < n; i++)
			printf("%02X", b ? b[i] : 0);
		printf("\n");
	} else if (!strncmp(l, "#preset_load=", 13)) {
		store_init();  /* store_preset = 0xFF: nothing loaded */
		api_init();
		api_init_mute();
		api_preset_load((uint8_t)atoi(l + 13));
		for (int i = 0; i < 12; i++)
			api_mainloop();  /* autounmute + mixer registers */
		printf("@preset:%u chksum_errors=%u\n", store_preset, store_chksum_error);
	} else if (!strncmp(l, "#pin=", 5)) {         /* #pin=PIN_NAME,v */
		char n[40];
		int v = 0;
		if (sscanf(l + 5, "%39[^,],%d", n, &v) == 2)
			host_pin_set(n, v);
	} else if (!strncmp(l, "#adc=", 5)) {         /* #adc=<0 ADCA|1 ADCB>,<ch>,<value> */
		unsigned a, c, v;
		sscanf(l + 5, "%u,%u,%u", &a, &c, &v);
		host_adc[a & 1][c & 1] = (uint16_t)v;
	} else if (!strncmp(l, "#rotary=", 8)) {      /* encoder value as left by the timer interrupt */
		timer_tce0_value = (int16_t)atoi(l + 8);
	} else if (!strcmp(l, "#rotary?")) {
		printf("@rotary:%d,%u,%u,%u\n", timer_tce0_value, timer_tce0_factor, timer_tce0_min, timer_tce0_max);
	} else if (!strncmp(l, "#reg=", 5)) {         /* #reg=<n>,<value>: register in RAM */
		unsigned n, v;
		sscanf(l + 5, "%u,%u", &n, &v);
		registers[n] = (uint8_t)v;
	} else if (!strncmp(l, "#sd_mkfs=", 9)) {     /* #sd_mkfs=<image>,<MB> */
		char path[300];
		unsigned mb = 8;
		sscanf(l + 9, "%299[^,],%u", path, &mb);
		printf("@sd_mkfs:%d\n", host_sd_image(path, mb));
	} else if (!strncmp(l, "#sd_put=", 8)) {      /* #sd_put=<name>,<hostfile>[,y,m,d,h,mi] */
		char name[40], hf[300];
		int y = 0, mo = 0, d = 0, h = 0, mi = 0;
		sscanf(l + 8, "%39[^,],%299[^,],%d,%d,%d,%d,%d", name, hf, &y, &mo, &d, &h, &mi);
		printf("@sd_put:%d\n", host_sd_put(name, hf, y, mo, d, h, mi));
	} else if (!strncmp(l, "#sd_card=", 9)) {
		host_sd_card(atoi(l + 9));
	} else if (!strcmp(l, "#sd_init")) {
		sd_init();
		printf("@sd_ready:%u\n", sd_ready);
	} else if (!strncmp(l, "#ms=", 4)) {          /* PORT (#60): advance tick_ms */
		host_ms = (uint16_t)(host_ms + atoi(l + 4));
	} else if (!strcmp(l, "#log_loop")) {
		log_mainloop();
	} else if (!strncmp(l, "#log_event=", 11)) {  /* #log_event=level,module,event,p1,p2 */
		unsigned a = 0, b = 0, c = 0, d = 0, e = 0;
		sscanf(l + 11, "%u,%u,%u,%u,%u", &a, &b, &c, &d, &e);
		log_event((uint8_t)a, (uint8_t)b, (uint8_t)c, (uint16_t)d, (uint16_t)e);
	} else if (!strncmp(l, "#sd_hex=", 8)) {      /* print a file of the mounted card as hex */
		FIL f;
		uint8_t buf[64];
		UINT n;
		printf("@sd_hex:");
		if (f_open(&f, l + 8, FA_READ) == FR_OK) {
			while (f_read(&f, buf, sizeof buf, &n) == FR_OK && n)
				for (UINT i = 0; i < n; i++)
					printf("%02X", buf[i]);
			f_close(&f);
		}
		printf("\n");
	} else if (!strncmp(l, "#sd_cat=", 8)) {      /* PORT (#24): print a file of the mounted card */
		FIL f;
		char line[200];
		if (f_open(&f, l + 8, FA_READ) == FR_OK) {
			while (f_gets(line, sizeof line, &f))
				printf("@sd_cat:%s", line);
			f_close(&f);
		} else
			printf("@sd_cat: no file\n");
	} else if (!strcmp(l, "#sd_load")) {
		sd_preset_load_init();
		printf("@sd_loaded preset=%u\n", store_preset);
	} else if (!strncmp(l, "#irbuf=", 7)) {
		host_ir_fill(l + 7);
	} else if (!strcmp(l, "#ir_init")) {
		irreceiver_init();
		ir_custom_init();
	} else if (!strcmp(l, "#ir_loop")) {
		irreceiver_mainloop();
		printf("@ir:%u,%u,%u value=%d\n", irreceiver_frames[1], irreceiver_frames[2], irreceiver_frames[3],
		       irreceiver_value);
	} else if (!strcmp(l, "#adc_init")) {
		events_adc_init();
		printf("@adc_limits:%u,%u,%u,%u,%u\n", events_adc_McuMaxTempOOR, events_adc_McuMaxTemp85C,
		       events_adc_McuMaxTemp90C, events_adc_McuMaxTemp95C, events_adc_McuMaxTemp100C);
	} else if (!strncmp(l, "#level_feed=", 12)) {   /* PORT (#51): #level_feed=<dBFS RMS>,<n samples> */
		double db = 0; int n = 0;
		sscanf(l + 12, "%lf,%d", &db, &n);
		double a = pow(10.0, db / 20.0);
		for (int i = 0; i < n; i++)
			level_feed(i & 1 ? -a : a);
	} else if (!strncmp(l, "#vu_feed=", 9)) {      /* PORT (#52): #vu_feed=<mixer input 1..8>,<dBFS RMS>,<n> */
		int g = 0, n = 0; double db = 0;
		sscanf(l + 9, "%d,%lf,%d", &g, &db, &n);
		double a = pow(10.0, db / 20.0);
		for (int i = 0; i < n; i++)
			level_vu_feed((uint8_t)g, i & 1 ? -a : a);
	} else if (!strncmp(l, "#vo_feed=", 9)) {      /* PORT (#52): #vo_feed=<output 1..8>,<dBFS RMS>,<n> */
		int g = 0, n = 0; double db = 0;
		sscanf(l + 9, "%d,%lf,%d", &g, &db, &n);
		double a = pow(10.0, db / 20.0);
		for (int i = 0; i < n; i++)
			level_vu_out_feed((uint8_t)g, i & 1 ? -a : a);
	} else if (!strcmp(l, "#vu?")) {
		printf("@vu:%u,%u\n", level_vu_active(), level_vu_out_active());
	} else if (!strcmp(l, "#level?")) {
		printf("@level:%u,%u\n", level_valid(), level_value());
	} else if (!strcmp(l, "#mixramp")) {          /* PORT (#50): one mixer ramp pass */
		api_mix_ramp_mainloop();
	} else if (!strcmp(l, "#level_loop")) {         /* one level_mainloop call (PEEK) */
		level_mainloop();
	} else if (!strcmp(l, "#supply_loop")) {     /* PORT (#46): 8 samples + 1 evaluation */
		for (int i = 0; i < 8; i++)
			supply_sample();
		supply_mainloop();
	} else if (!strcmp(l, "#adc_loop")) {        /* 8 samples and the averages */
		for (int i = 0; i < 8; i++)
			events_adc_mainloop_32();
		events_adc_mainloop_16();
		printf("@adc:%u,%u,%u,%u\n", events_adc_values[1], events_adc_values[2], events_adc_values[3],
		       events_adc_values[4]);
	} else if (!strcmp(l, "#adc_41")) {
		events_adc_mainloop_41();
		printf("@unmute:%d\n", api_autounmute_counter);
	} else if (!strcmp(l, "#modc_init")) {
		ctrl_modc_init();
	} else if (!strcmp(l, "#modc")) {             /* one MODC pass */
		ctrl_modc_mainloop();
	} else if (!strcmp(l, "#events_init")) {
		ctrl_events_init();
		events_spdif_init();
	} else if (!strcmp(l, "#events_poll")) {
		events_poll_pins();
	} else if (!strcmp(l, "#events_overflow")) {
		events_overflow_mainloop();
	} else if (!strcmp(l, "#events_spdif")) {
		events_spdif_mainloop();
	} else if (!strncmp(l, "#event?", 7)) {      /* #event?N: value and change counter */
		int n = atoi(l + 7);
		printf("@event%d:%u,%u\n", n, events_field_value[n], events_is_changed[n]);
	} else if (!strncmp(l, "#factory=", 9)) {     /* FORGE_FACTORY_RESET at run time */
		extern uint8_t factory_reset_enabled;
		factory_reset_enabled = (uint8_t)atoi(l + 9);
	} else if (!strcmp(l, "#at24_detect")) {
		at24c512_detect();
		printf("@hw:%04X\n", hardware_other);
	} else if (!strcmp(l, "#sys_init_loaded")) {   /* #40: as after the first preset load */
		void sys_init_preset_loaded(void);
		sys_init_preset_loaded();
	} else if (!strcmp(l, "#sys_init_loop")) {
		void sys_init_mainloop(void);
		sys_init_mainloop();
	} else if (!strcmp(l, "#sys_init_init")) {
		void sys_init_init(void);
		sys_init_init();
	} else if (!strcmp(l, "#store_init")) {
		store_init();
	} else if (!strcmp(l, "#mainloop")) {        /* one main loop of the audio part */
		ctrl_modc_mainloop();
		api_mainloop();
		store_mainloop();
	} else if (!strcmp(l, "#dsp_detect")) {
		for (uint8_t n = 1; n <= BAS_MODUL_TAS3108_CORES; n++)
			dsp_core_init(n);
	} else if (!strcmp(l, "#systeminit")) {
		flavour_systeminit_peripherals();
		printf("@hw:%04X dsp:%04X\n", hardware_other, hardware_dsp);
	} else if (!strcmp(l, "#bqramp")) {             /* #44: one clickless step */
		bqramp_mainloop();
		printf("@bqramp:%u\n", bqramp_active());
	} else if (!strcmp(l, "#mutechg")) {            /* mute priority "change" (auto unmute) */
		printf("@mutechg:%u\n", api_get_out_mute_prio(api_mute_out_prio_change));
	} else if (!strncmp(l, "#mixval=", 8)) {         /* #14: effective mixer value c,g */
		int c = 0, g = 0;
		sscanf(l + 8, "%d,%d", &c, &g);
		printf("@mix:%u\n", api_mix_value((uint8_t)c, (uint8_t)g));
	} else if (!strcmp(l, "#hw_check")) {           /* systeminit "checks evaluate" (#32) */
		uint8_t bad = hw_check_evaluate();
		printf("@hwcheck:%u mute:%u\n", bad, api_get_out_mute_prio(api_mute_out_prio_hardware));
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

/* internal EEPROM and the external ones that were asked for (#dspee_out, #at24out) */
static __attribute__((unused)) void save_all(void)
{
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
}

#ifdef SIM
/* Simulator for the GUI (tools/sim, forge-dsp-gui-ng #11): the USB output collects in a buffer,
 * a reset is remembered and the caller boots again. */
static char sim_out[1u << 16];
static unsigned sim_n;
static int sim_reset;
#endif

void mcu_reset_delayed(void)
{
#ifdef SIM
	sim_reset = 1;
	return;
#else
	printf("\n@reset\n");
	save_all();
	exit(0);
#endif
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
#ifdef SIM
	if (buart == 0 && sim_n < sizeof sim_out - 1)
		sim_out[sim_n++] = c;
	return;
#endif
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

/* start as after power-on with the EEPROM in eeprom[] (0xFF everywhere: sysinit) */
static __attribute__((unused)) void host_boot(void)
{
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
}

/* one input line: "#..." harness command, "<ch>|<command>" to interface ch (1 = USB);
 * returns 1 if it was a command to an interface */
static int host_line(char *line)
{
	line[strcspn(line, "\r\n")] = 0;
	if (!strncmp(line, "#dtr=", 5)) { host_dtr = atoi(line + 5); return 0; }
	if (!strncmp(line, "#ir=", 4)) { host_ir = atoi(line + 4); return 0; }
	if (line[0] == '#') { store_cmd(line); return 0; }
	int ch = line[0] - '0';
	if (ch < 1 || ch > 4 || line[1] != '|')
		return 0;
	snprintf((char *)rx[ch], sizeof rx[ch], "%.125s\r\n", line + 2);
	iface_todo[ch] = 1;
	cmd_check4buffer();
	return 1;
}

#ifdef SIM
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define SIM_API EMSCRIPTEN_KEEPALIVE
#else
#define SIM_API
#endif
/* the board as a fully populated Forge: every chip the flavour knows answers; the MCU temperature
 * sensor reads about 38 °C (ADCB 0 = 2251 with the calibration 0x0A00 of the harness) */
static const char *const sim_board[] = {
	"#i2cdev=1,E0", "#i2cdev=1,40", "#i2cdev=1,4E", "#i2cdev=2,90", "#i2cdev=1,20", "#i2cdev=1,22",
	"#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#i2cdev=2,30", "#i2cdev=1,98", "#i2cdev=2,20",
	"#adc=1,0,2251",
};
#include "factory.h"
#include "ir.h"
#include "limiter.h"
#include "loopcall.h"
#include "sd.h"
#include "supply.h"
void sys_init_init(void);
static uint16_t sim_counter, sim_zero;
static uint32_t sim_us;
/* the EEPROM (4096 bytes) to load before sim_boot or to save after commands */
SIM_API uint8_t *sim_eeprom(void) { return eeprom; }
/* the preset EEPROM (AT24C512, 65536 bytes), the same way; 0xFF everywhere = blank */
SIM_API uint8_t *sim_at24(void) { return at24; }
/* start as main.c does after power-on (FORGE_BRINGUP); returns 1 if a reset was pending */
SIM_API int sim_boot(void)
{
	char l[64];
	int r = sim_reset;
	sim_reset = 0;
	errorcount_init();
	registers_load();
	sysinit_check(0);
	log_event(LOG_FATAL, LOGM_SYS, LOGE_SYS_START, 0, 0);
	i2c_bus_init(1);
	i2c_bus_init(2);
	static int first = 1;   /* the external EEPROMs keep their content over a restart, like the chips */
	if (first) {
		first = 0;
		memset(dspee, 0xFF, sizeof dspee);
	}
	for (unsigned i = 0; i < sizeof sim_board / sizeof sim_board[0]; i++) {
		snprintf(l, sizeof l, "%s", sim_board[i]);
		host_line(l);
	}
	flavour_systeminit_peripherals();
	hw_check_dsp_restart();
	limiter_apply();
	api_init();
	api_init_mute();
	sd_init();
	irreceiver_init();
	ir_custom_init();
	sys_init_init();
	ctrl_events_init();
	events_poll_pins();
	events_adc_init();
	supply_init();
	level_init();
	events_spdif_init();
	ctrl_modc_init();
	hw_check_evaluate();
	cmd_init();
	sd_preset_load_init();
	sysinit_count_boot();
	cmd_req_value[0] = 'o'; cmd_req_value[1] = 'k'; cmd_req_value[2] = 0;
	if (factory_write()) {   /* end of systeminit.bas after a factory reset: event, done, reset */
		static const uint16_t reinit_event[cmd_req_words_max] = {0x0104, 0x7379, 0x7265, 0x0000, 0x0000};
		cmd_load_header(reinit_event);
		cmd_sendevent();
		set_register(BAS_REG_STARTUP_STATE, BAS_STARTUP_STATE_DONE);
		mcu_reset_delayed();
	}
	static const uint16_t bootrdy_event[cmd_req_words_max] = {0x0104, 0x7379, 0x626F, 0x0000, 0x0000};
	cmd_load_header(bootrdy_event);
	cmd_sendevent();
	sim_n = 0;  /* the boot messages are not for the GUI */
	return r;
}
/* one command line from the GUI to the USB port (without CR/LF) */
SIM_API void sim_command(const char *cmd)
{
	char l[160];
	snprintf(l, sizeof l, "1|%s", cmd);
	host_line(l);
}
/* run the firmware for ms milliseconds: the TCE0 tick (1.954 ms) with the loopcall bits of tick.c */
SIM_API void sim_loop(uint16_t ms)
{
	sim_us += (uint32_t)ms * 1000u;
	while (sim_us >= 1954u) {
		sim_us -= 1954u;
		host_ms = (uint16_t)(host_ms + 2);
		uint16_t c = ++sim_counter;
		uint16_t bits = (uint16_t)(~sim_zero & c);
		sim_zero = c;
		loopcall(bits);
	}
}
/* USB output since the last call (NUL-terminated), and whether a reset is pending */
SIM_API const char *sim_output(void) { sim_out[sim_n] = 0; sim_n = 0; return sim_out; }
SIM_API int sim_reset_pending(void) { return sim_reset; }
#else
/* usage: cmd-<flavour> [EEPROM_IN|- [EEPROM_OUT]]  (default: blank 0xFF, i.e. sysinit) */
int main(int argc, char **argv)
{
	char line[4096];
	memset(eeprom, 0xFF, sizeof eeprom);
	if (argc > 1 && strcmp(argv[1], "-")) {
		FILE *f = fopen(argv[1], "rb");
		if (!f || fread(eeprom, 1, sizeof eeprom, f) == 0) { perror(argv[1]); return 2; }
		fclose(f);
	}
	if (argc > 2)
		eeprom_out = argv[2];
	host_boot();
	while (fgets(line, sizeof line, stdin)) {
		if (!host_line(line))
			continue;
		if (cur >= 0) { printf("\n"); cur = -1; }
		printf("--\n");
	}
	save_all();
	return 0;
}
#endif
