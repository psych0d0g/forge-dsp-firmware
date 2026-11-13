/*
 * Input level measurement for the loudness block (port extension, issue #51), see level.h.
 * Sample format (measured on the device): the 32-bit input word sits 8 bits up in the 48-bit
 * data word, so full scale is 2^39 in the raw integer.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include <math.h>
#include "level.h"
#include "dsp.h"
#include "modc.h"
#include "dsp_pool.h"

static uint8_t mask, next_in;
static double tau = 3.0;
static double block_sum;            /* power sum of the running block */
static uint8_t block_n;
static double hist[LEVEL_MEDIAN];   /* dB of the last blocks above the gate */
static uint8_t hist_n, hist_i;
static double smooth_db;            /* smoothed level, dB */
static uint8_t valid, value;        /* value: reported level with hysteresis */
static double vu_pow[8];            /* level meter (#52): smoothed power per mixer input */
static uint8_t vu_hold, vu_pair;
static double vo_pow[8];            /* output levels (#52) */
static uint8_t vo_hold, vo_pair;

void level_init(void)
{
	mask = next_in = 0;
	block_sum = 0.0;
	block_n = hist_n = hist_i = 0;
	smooth_db = -120.0;
	valid = 0;
	value = 0;
	vu_hold = vu_pair = vo_hold = vo_pair = 0;
	for (uint8_t i = 0; i < 8; i++)
		vu_pow[i] = vo_pow[i] = 0.0;
}

void level_config(uint8_t m, uint8_t tau_tenths)
{
	if (m != mask) {
		mask = m;
		next_in = 0;
	}
	tau = (tau_tenths ? tau_tenths : 30) / 10.0;
}

uint8_t level_valid(void) { return valid; }

static uint8_t to_scale(double db)
{
	double v = 255.0 + 2.0 * db;  /* 0.5 dB per step */
	if (v < 0.0)
		return 0;
	if (v > 255.0)
		return 255;
	return (uint8_t)(v + 0.5);
}

uint8_t level_value(void) { return value; }

static double median(void)
{
	double s[LEVEL_MEDIAN];
	uint8_t n = hist_n;
	for (uint8_t i = 0; i < n; i++)
		s[i] = hist[i];
	for (uint8_t i = 1; i < n; i++)  /* insertion sort, n <= LEVEL_MEDIAN */
		for (uint8_t j = i; j > 0 && s[j - 1] > s[j]; j--) {
			double t = s[j];
			s[j] = s[j - 1];
			s[j - 1] = t;
		}
	return s[n / 2];
}

static void block_done(double db)
{
	hist[hist_i] = db;
	hist_i = (uint8_t)((hist_i + 1) % LEVEL_MEDIAN);
	if (hist_n < LEVEL_MEDIAN)
		hist_n++;
	double m = median();
	if (!valid) {
		smooth_db = m;
		valid = 1;
	} else {
		double a = LEVEL_BLOCK_S / tau;
		if (a > 1.0)
			a = 1.0;
		smooth_db += (m - smooth_db) * a;
	}
	uint8_t v = to_scale(smooth_db);
	/* hysteresis: follow only changes of more than LEVEL_HYST_STEPS (or the first value) */
	if (value == 0 || v > value + LEVEL_HYST_STEPS || v + LEVEL_HYST_STEPS < value) {
		value = v;
		modc_loudness_touch();
	}
}

void level_feed(double sample)
{
	block_sum += sample * sample;
	if (++block_n < LEVEL_BLOCK_N)
		return;
	double p = block_sum / block_n;
	block_sum = 0.0;
	block_n = 0;
	if (p < LEVEL_GATE_POWER)
		return;  /* pause: hold */
	block_done(10.0 * log10(p));
}

/* --- level meter (#52) -------------------------------------------------------------------------
 * mixer input G (1..8) -> data memory address of its sample: G1..G8 = in5, in6, in3, in4, in1,
 * in2, in8, in7 (docs/dsp-images.md), in N at D_(N-1) */
static const uint8_t vu_addr[8] = {4, 5, 2, 3, 0, 1, 7, 6};

uint8_t level_vu_active(void) { return vu_hold > 0; }

void level_vu_feed(uint8_t g, double sample)
{
	if (g < 1 || g > 8)
		return;
	vu_pow[g - 1] += (sample * sample - vu_pow[g - 1]) * LEVEL_VU_ALPHA;
}

void level_vu_get(uint8_t out[8])
{
	if (!vu_hold)  /* was off: old values say nothing about now */
		for (uint8_t i = 0; i < 8; i++)
			vu_pow[i] = 0.0;
	vu_hold = LEVEL_VU_HOLD;
	for (uint8_t i = 0; i < 8; i++)
		out[i] = vu_pow[i] > 1e-13 ? to_scale(10.0 * log10(vu_pow[i])) : 0;
}

static uint8_t peek_sample_core(uint8_t core, uint16_t addr, double *x)
{
	uint8_t b[8];
	if (!dsp_peek(core, 2, addr, b))
		return 0;
	int64_t v = 0;
	for (int8_t i = 5; i >= 0; i--)
		v = (v << 8) | b[i];
	if (v & ((int64_t)1 << 47))
		v -= (int64_t)1 << 48;
	*x = (double)v / 549755813888.0;  /* 2^39 = full scale */
	return 1;
}

static uint8_t peek_sample(uint8_t addr, double *x) { return peek_sample_core(1, addr, x); }

/* output n (1..8) -> DO slot of its core: outputs 1/2 = DO3/DO4 (SDOUT2), 3/4 = DO1/DO2 (SDOUT1),
 * measured on the V40-96ELN (#47); core 3 carries outputs 1-4, core 2 outputs 5-8 */
static const uint8_t vo_do[4] = {2, 3, 0, 1};

uint8_t level_vu_out_active(void) { return vo_hold > 0; }

void level_vu_out_feed(uint8_t n, double sample)
{
	if (n < 1 || n > 8)
		return;
	vo_pow[n - 1] += (sample * sample - vo_pow[n - 1]) * LEVEL_VU_ALPHA;
}

uint8_t level_vu_out_get(uint8_t out[8])
{
	if (!vo_hold)
		for (uint8_t i = 0; i < 8; i++)
			vo_pow[i] = 0.0;
	uint8_t known = dsp_do_word(0) != 0;
	vo_hold = known ? LEVEL_VU_HOLD : 0;
	for (uint8_t i = 0; i < 8; i++)
		out[i] = vo_pow[i] > 1e-13 ? to_scale(10.0 * log10(vo_pow[i])) : 0;
	return known;
}

static void vo_mainloop(void)
{
	if (!vo_hold)
		return;
	vo_hold--;
	for (uint8_t k = 0; k < 2; k++) {
		uint8_t n = (uint8_t)(2 * vo_pair + k + 1);
		double x;
		if (peek_sample_core(n <= 4 ? 3 : 2, dsp_do_word(vo_do[(n - 1) % 4]), &x))
			level_vu_out_feed(n, x);
	}
	vo_pair = (uint8_t)((vo_pair + 1) % 4);
}

static void vu_mainloop(void)
{
	if (!vu_hold)
		return;
	vu_hold--;
	for (uint8_t k = 0; k < 2; k++) {
		uint8_t g = (uint8_t)(2 * vu_pair + k + 1);
		double x;
		if (peek_sample(vu_addr[g - 1], &x))
			level_vu_feed(g, x);
	}
	vu_pair = (uint8_t)((vu_pair + 1) % 4);
}

void level_mainloop(void)
{
	vu_mainloop();
	vo_mainloop();
	if (!mask)
		return;
	for (uint8_t k = 0; k < 8; k++) {
		uint8_t in = (uint8_t)((next_in + k) % 8);
		if (mask & (1u << in)) {
			next_in = (uint8_t)((in + 1) % 8);
			double x;
			if (peek_sample(in, &x))
				level_feed(x);
			return;
		}
	}
}
