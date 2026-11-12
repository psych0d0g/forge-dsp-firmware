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
static double slow, fast;
static uint8_t valid, last_value;

void level_init(void)
{
	mask = next_in = 0;
	slow = fast = 0.0;
	valid = 0;
	last_value = 0;
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

static uint8_t to_scale(double p)
{
	if (p <= 1e-12)
		return 0;
	double v = 255.0 + 2.0 * 10.0 * log10(p);  /* 0.5 dB per step */
	if (v < 0.0)
		return 0;
	if (v > 255.0)
		return 255;
	return (uint8_t)(v + 0.5);
}

uint8_t level_value(void) { return to_scale(slow); }

void level_feed(double sample)
{
	double p = sample * sample;
	/* one sample per call over all selected inputs: the average is their mean power */
	const double dt = 1.0 / LEVEL_RATE_HZ;
	fast += (p - fast) * (dt / LEVEL_FAST_S);
	if (fast < LEVEL_GATE_POWER)
		return;  /* pause: hold */
	if (!valid) {
		slow = fast;  /* first level: start from the fast average */
		valid = 1;
	} else {
		slow += (p - slow) * (dt / tau);
	}
	uint8_t v = to_scale(slow);
	if (v != last_value) {
		last_value = v;
		modc_loudness_touch();
	}
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
