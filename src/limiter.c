/*
 * PORT extension (#58): output limiter, see include/limiter.h and tools/dsp_limiter.py.
 * Per output (DO slot of core 3 for outputs 1-4, core 2 for 5-8): [1/T, T]; per core
 * [attack factor, 1 - release, release]. A core whose outputs are all off gets attack = 1 and the
 * block is bit transparent there.
 *   set system.dsp.limiter.N=T      T in tenths of dBFS, 0 = off, -240 .. 0
 *   set system.dsp.limiter.attack=A tenths of ms (1 .. 1000), set system.dsp.limiter.release=R ms (1 .. 5000)
 *   get system.dsp.limiter = [T1..T8], .attack, .release
 * License: CC BY-NC-SA 4.0
 */
#include <math.h>
#include "limiter.h"
#include "biquad.h"
#include "cmd.h"
#include "dsp.h"
#include "eram.h"
#include "flavour.h"
#include "i2c.h"
#include "tas3108.h"

#include "dsp_pool.h"

#define EE_MAGIC   0x0F30   /* 0x4C = valid */
#define EE_T       0x0F31   /* 8 bytes: threshold in -0.5 dB steps (0 = off, 1..48) */
#define EE_ATTACK  0x0F39   /* word: tenths of ms */
#define EE_RELEASE 0x0F3B   /* word: ms */
#define MAGIC 0x4C
#define DEF_ATTACK 1        /* 0.1 ms */
#define DEF_RELEASE 100     /* ms */

/* output n (1..8) -> DO slot of its core, as the level meter (#52) */
static const uint8_t do_slot[4] = {2, 3, 0, 1};

uint8_t limiter_available(void) { return dsp_limiter_sub(0) != 0; }

static uint8_t valid(void) { return eram_rb(EE_MAGIC) == MAGIC; }

int16_t limiter_threshold(uint8_t out)
{
	if (!valid() || out < 1 || out > 8)
		return 0;
	uint8_t s = eram_rb((uint16_t)(EE_T + out - 1));
	return s > 48 ? 0 : (int16_t)(-5 * (int16_t)s);
}

uint16_t limiter_attack(void)
{
	uint16_t v = valid() ? eram_rw(EE_ATTACK) : 0;
	return v >= 1 && v <= 1000 ? v : DEF_ATTACK;
}

uint16_t limiter_release(void)
{
	uint16_t v = valid() ? eram_rw(EE_RELEASE) : 0;
	return v >= 1 && v <= 5000 ? v : DEF_RELEASE;
}

static void make_valid(void)
{
	if (valid())
		return;
	for (uint8_t i = 0; i < 8; i++)
		eram_wb((uint16_t)(EE_T + i), 0);
	eram_ww(EE_ATTACK, DEF_ATTACK);
	eram_ww(EE_RELEASE, DEF_RELEASE);
	eram_wb(EE_MAGIC, MAGIC);
}

uint8_t limiter_set_threshold(uint8_t out, int16_t tenths)
{
	if (out < 1 || out > 8 || tenths > 0 || tenths < LIMITER_MIN_TENTHS)
		return 0;
	make_valid();
	eram_wb((uint16_t)(EE_T + out - 1), (uint8_t)((-tenths + 2) / 5));  /* 0.5 dB steps */
	return 1;
}

uint8_t limiter_set_attack(uint16_t v)
{
	if (v < 1 || v > 1000)
		return 0;
	make_valid();
	eram_ww(EE_ATTACK, v);
	return 1;
}

uint8_t limiter_set_release(uint16_t v)
{
	if (v < 1 || v > 5000)
		return 0;
	make_valid();
	eram_ww(EE_RELEASE, v);
	return 1;
}

/* buffer_dwords for a register of n words: the lowest address is sent first = dword n */
static void put_words(const uint32_t *w, uint8_t n)
{
	for (uint8_t j = 0; j < n; j++) {
		uint8_t at = (uint8_t)((n - 1 - j) * 4 + 1);
		for (uint8_t b = 0; b < 4; b++)
			buffer_bytes[at + b] = (uint8_t)(w[j] >> (8 * b));
	}
}

static void write_reg(uint8_t core, uint8_t sub, const uint32_t *w, uint8_t n)
{
	dsp_core = core;
	dsp_reg = sub;
	put_words(w, n);
	dsp_set_reg((uint8_t)(n * 4));
}

void limiter_apply(void)
{
	if (!limiter_available())
		return;
	const double fs = BAS_FLOW_SAMPLE_RATE;
	uint8_t on[4] = {0};
	for (uint8_t n = 1; n <= 8; n++) {
		int16_t t = limiter_threshold(n);
		uint8_t core = n <= 4 ? 3 : 2;
		uint32_t w[2];
		if (t == 0) {
			w[0] = w[1] = 0x00800000UL;
		} else {
			double lin = pow(10.0, t / 200.0);
			w[0] = dsp_math_to523(1.0 / lin);
			w[1] = dsp_math_to523(lin);
			on[core] = 1;
		}
		write_reg(core, dsp_limiter_sub(do_slot[(n - 1) % 4]), w, 2);
	}
	double rel = 1.0 - exp(-1000.0 / (limiter_release() * fs));
	uint32_t r = dsp_math_to523(rel);
	for (uint8_t core = 2; core <= 3; core++) {
		uint32_t w[3];
		w[0] = on[core] ? dsp_math_to523(exp(-10000.0 / (limiter_attack() * fs))) : 0x00800000UL;
		w[1] = 0x00800000UL - r;  /* 1 - rel exactly, so that g = 1 stays 1 */
		w[2] = r;
		write_reg(core, dsp_limiter_sub(4), w, 3);
	}
}

/* --- commands ------------------------------------------------------------------------------ */
static void out_limits(void)
{
	cmd_answer_header();
	out_chr('[');
	for (uint8_t n = 1; n <= 8; n++) {
		if (n > 1)
			out_chr(',');
		out_i16(limiter_threshold(n));
	}
	out_chr(']');
	out_crlf();
}

void cmd_get_system_dsp(void)
{
	if (!limiter_available() || req_w(cmd_req_word_three) != 0x6C69) {  /* limiter */
		cmd_error_unknown();
		return;
	}
	uint16_t w4 = req_w(cmd_req_word_four);
	if (REQ_B(10) == cmd_req_number) {
		uint8_t n = REQ_B(9);
		if (n < 1 || n > 8) { cmd_error_cmd_oor(); return; }
		cmd_answer_header(); out_i16(limiter_threshold(n)); out_crlf();
	} else if (w4 == 0x0000 || w4 == 0x002A) {
		out_limits();
	} else if (w4 == 0x6174) {   /* attack */
		cmd_answer_header(); out_u16(limiter_attack()); out_crlf();
	} else if (w4 == 0x7265) {   /* release */
		cmd_answer_header(); out_u16(limiter_release()); out_crlf();
	} else {
		cmd_error_unknown();
	}
}

void cmd_set_system_dsp(void)
{
	if (!limiter_available() || req_w(cmd_req_word_three) != 0x6C69) {
		cmd_error_unknown();
		return;
	}
	int16_t v = cmd_reg_var_integers[1];
	uint16_t w4 = req_w(cmd_req_word_four);
	uint8_t ok;
	if (REQ_B(10) == cmd_req_number)
		ok = limiter_set_threshold(REQ_B(9), v);
	else if (w4 == 0x6174)
		ok = v > 0 && limiter_set_attack((uint16_t)v);
	else if (w4 == 0x7265)
		ok = v > 0 && limiter_set_release((uint16_t)v);
	else {
		cmd_error_unknown();
		return;
	}
	if (!ok) {
		cmd_error_cmd_oor();
		return;
	}
	limiter_apply();
	cmd_answer_ok();
}
