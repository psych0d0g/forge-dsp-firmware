/*
 * Port of libs/cmd/v20/common/subs/cmd_delay (version 20).
 * "delay.N" / "delay.out.N" (offset 0/1), optional unit .cm/.inch/.ms; values in samples,
 * stored doubled with feature_*_delay_mode.
 * PORT deviations (all in the original's error paths or overflows):
 * - delay.in writes only the input delay channels; the original loops/permits up to
 *   flow_max_out_channel and writes channels 7/8 over output delays 1/2 (contiguous storage).
 *   Reading keeps the original's 8 values (7/8 = output delays 1/2) for the GUI.
 * - unknown unit on set: only the error (the original also answers ok and sets a stale value)
 * - ".*" sets the channel byte of the path that was used (the original always sets byte 5)
 * - reading with a unit prints the value (the original prints only the header, issue #21)
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include <math.h>
#include "cmd.h"
#include "api.h"
#include "dsp.h"
#include "flavour.h"

static uint8_t unit_ok(uint16_t unit, double *factor)
{
	switch (unit) {
	case 0x636D: *factor = BAS_CMD_FACTOR_CM; return 1;    /* cm */
	case 0x696E: *factor = BAS_CMD_FACTOR_INCH; return 1;  /* inch */
	case 0x6D73: *factor = BAS_CMD_FACTOR_MS; return 1;    /* ms */
	}
	return 0;
}

/* value of cmd_reg_var 1 in samples; 0 = error answered */
static uint8_t value_samples(uint8_t unit_word, uint16_t *w)
{
	uint16_t unit = req_w(unit_word);
	if (unit == 0x0000) {
		*w = (uint16_t)cmd_reg_var_integers[1];
		return 1;
	}
	double f;
	if (!(cmd_reg_var_states[1] & (1 << cmd_reg_state_isSingle)) || !unit_ok(unit, &f)) {
		cmd_error_unknown();
		return 0;
	}
	*w = (uint16_t)(int32_t)(cmd_reg_var_singles[1] / f);  /* l = singles(2): long, then word */
	return 1;
}

/* print a double with up to 3 decimals */
static void out_value(double s)
{
	int32_t v = (int32_t)lround(s * 1000.0);
	if (v < 0) {
		out_chr('-');
		v = -v;
	}
	out_u16((uint16_t)(v / 1000));  /* delays: at most 5840 samples */
	uint16_t frac = (uint16_t)(v % 1000);
	if (frac) {
		out_chr('.');
		char d[4] = {(char)('0' + frac / 100), (char)('0' + frac / 10 % 10), (char)('0' + frac % 10), 0};
		for (int8_t i = 2; i > 0 && d[i] == '0'; i--)
			d[i] = 0;
		out_str(d);
	}
}

static void print_one(uint8_t unit_word, uint16_t w)
{
	uint16_t unit = req_w(unit_word);
	double f;
	if (unit == 0x0000) {
		cmd_answer_header();
		out_u16(w);
		out_crlf();
	} else if (unit_ok(unit, &f)) {
		cmd_answer_header();
		out_value(w * f);
		out_crlf();
	} else {
		cmd_error_unknown();
	}
}

#if BAS_FEATURE_INPUT_DELAY > 0
#define IN_MODE BAS_FEATURE_INPUT_DELAY_MODE
static uint16_t get_in(uint8_t ch)
{
	/* contiguous storage as in the original: channels beyond the inputs are the outputs */
	uint16_t w = ch <= BAS_STORE_DELAY_IN_COUNT ? api_get_delay_in(ch) : api_get_delay((uint8_t)(ch - BAS_STORE_DELAY_IN_COUNT));
	return IN_MODE > 0 ? w / 2 : w;
}
#endif
static uint16_t get_out(uint8_t ch)
{
	uint16_t w = api_get_delay(ch);
	return BAS_FEATURE_OUTPUT_DELAY_MODE > 0 ? w / 2 : w;
}

static void set_one(uint8_t in, uint8_t ch, uint16_t w)
{
	cmd_answer_ok();
#if BAS_FEATURE_INPUT_DELAY > 0
	if (in) {
		api_set_delay_in(ch, IN_MODE > 0 ? (uint16_t)(w * 2) : w);
		dsp_1_ack_delay();
		return;
	}
#else
	(void)in;
#endif
	api_set_delay(ch, BAS_FEATURE_OUTPUT_DELAY_MODE > 0 ? (uint16_t)(w * 2) : w);
	dsp_ack_delay();
}

/* offset 0: delay.N.unit, 1: delay.out.N.unit / delay.in.N.unit */
static void set_delay(uint8_t in, uint8_t offset)
{
	uint8_t w2 = (uint8_t)(cmd_req_word_two + offset), w3 = (uint8_t)(cmd_req_word_three + offset);
	uint8_t b5 = (uint8_t)(5 + offset * 2), b6 = (uint8_t)(6 + offset * 2);
	uint8_t max = BAS_FLOW_MAX_OUT_CHANNEL;
#if BAS_FEATURE_INPUT_DELAY > 0
	if (in)
		max = BAS_STORE_DELAY_IN_COUNT;
#endif
	if (req_w(w2) == 0x0000) {  /* all channels: [a,b,...] */
		cmd_answer_ok();
		for (uint8_t i = 1; i <= max; i++) {
			uint16_t w = (uint16_t)cmd_reg_var_integers[i];
#if BAS_FEATURE_INPUT_DELAY > 0
			if (in) {
				api_set_delay_in(i, IN_MODE > 0 ? (uint16_t)(w * 2) : w);
				continue;
			}
#endif
			api_set_delay(i, BAS_FEATURE_OUTPUT_DELAY_MODE > 0 ? (uint16_t)(w * 2) : w);
		}
#if BAS_FEATURE_INPUT_DELAY > 0
		if (in) {
			dsp_1_ack_delay();
			return;
		}
#endif
		dsp_ack_delay();
		return;
	}
	if (REQ_B(b6) != cmd_req_number) {
		cmd_error_unknown();
		return;
	}
	uint8_t ch = REQ_B(b5);
	if (ch < 1 || ch > max) {
		cmd_error_cmd_oor();
		return;
	}
	uint16_t w;
	if (value_samples(w3, &w))
		set_one(in, ch, w);
}

void cmd_set_delay(void)
{
	api_set_out_mute_change();
	switch (req_w(cmd_req_word_two)) {
#if BAS_FEATURE_INPUT_DELAY > 0
	case 0x696E: set_delay(1, 1); break;
#endif
	case 0x6F75: set_delay(0, 1); break;
	default:
		if (req_w(cmd_req_word_four) != 0x0000)
			cmd_error_unknown();
		else
			set_delay(0, 0);
	}
}

static void get_delay(uint16_t (*get)(uint8_t), uint8_t offset)
{
	uint8_t w2 = (uint8_t)(cmd_req_word_two + offset), w3 = (uint8_t)(cmd_req_word_three + offset);
	uint8_t b5 = (uint8_t)(5 + offset * 2), b6 = (uint8_t)(6 + offset * 2);
	const uint8_t max = BAS_FLOW_MAX_OUT_CHANNEL;
	if (REQ_B(b6) == cmd_req_number) {
		uint8_t ch = REQ_B(b5);
		if (ch < 1 || ch > max)
			cmd_error_cmd_oor();
		else
			print_one(w3, get(ch));
		return;
	}
	switch (req_w(w2)) {
	case 0x0000:
		cmd_answer_header();
		out_chr('[');
		for (uint8_t i = 1; i <= max; i++) {
			out_u16(get(i));
			if (i < max)
				out_chr(',');
		}
		out_chr(']');
		out_crlf();
		break;
	case 0x002A:
		REQ_B(b6) = cmd_req_number;
		for (uint8_t i = 1; i <= max; i++) {
			REQ_B(b5) = i;
			print_one(w3, get(i));
		}
		break;
	case 0x6C65: {  /* left: free samples per DSP */
		uint8_t a = 1, b = BAS_FLOW_MAX_OUT_CHANNEL / BAS_FLOW_MAX_DSP;
		cmd_answer_header();
		out_chr('[');
		for (uint8_t i = 1; i <= BAS_FLOW_MAX_DSP; i++) {
			uint16_t w = BAS_FLOW_MAX_SAMPLES;
			for (uint8_t ii = 1; ii <= b; ii++)
				w = (uint16_t)(w - get(a++));
			out_u16(w);
			if (i < BAS_FLOW_MAX_DSP)
				out_chr(',');
		}
		out_chr(']');
		out_crlf();
		break;
	}
	default: cmd_error_unknown();
	}
}

void cmd_get_delay(void)
{
	switch (req_w(cmd_req_word_two)) {
#if BAS_FEATURE_INPUT_DELAY > 0
	case 0x696E: get_delay(get_in, 1); break;
#endif
	case 0x6F75: get_delay(get_out, 1); break;
	default:
		if (req_w(cmd_req_word_four) != 0x0000)
			cmd_error_unknown();
		else
			get_delay(get_out, 0);
	}
}
