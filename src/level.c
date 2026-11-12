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

static uint8_t mask, next_in;
static double tau = 3.0;
static double block_sum;            /* power sum of the running block */
static uint8_t block_n;
static double hist[LEVEL_MEDIAN];   /* dB of the last blocks above the gate */
static uint8_t hist_n, hist_i;
static double smooth_db;            /* smoothed level, dB */
static uint8_t valid, value;        /* value: reported level with hysteresis */

void level_init(void)
{
	mask = next_in = 0;
	block_sum = 0.0;
	block_n = hist_n = hist_i = 0;
	smooth_db = -120.0;
	valid = 0;
	value = 0;
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

void level_mainloop(void)
{
	if (!mask)
		return;
	for (uint8_t k = 0; k < 8; k++) {
		uint8_t in = (uint8_t)((next_in + k) % 8);
		if (mask & (1u << in)) {
			next_in = (uint8_t)((in + 1) % 8);
			uint8_t b[8];
			if (!dsp_peek(1, 2, in, b))
				return;
			int64_t x = 0;
			for (int8_t i = 5; i >= 0; i--)
				x = (x << 8) | b[i];
			if (x & ((int64_t)1 << 47))
				x -= (int64_t)1 << 48;
			level_feed((double)x / 549755813888.0);  /* 2^39 = full scale */
			return;
		}
	}
}
