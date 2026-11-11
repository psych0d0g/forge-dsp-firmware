/*
 * Port of libs/cmd/v20/common/subs/cmd_gain, cmd_mute, cmd_phase (version 20).
 * PORT: channel numbers are checked against the channel count of the group (BASCOM checks
 * gain.in/gain.spdif against flow_max_out_channel and writes beyond storage_vol_in into the
 * next field) and channel 0 is rejected (BASCOM writes the field before the array).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include <stddef.h>
#include "cmd.h"
#include "api.h"
#include "flavour.h"

#define CH REQ_B(7)
#define IS_NUM (REQ_B(8) == cmd_req_number)

static uint8_t ch_ok(uint8_t max)
{
	if (CH < 1 || CH > max) {
		cmd_error_cmd_oor();
		return 0;
	}
	return 1;
}

/* --- gain --- */

void cmd_set_gain(void)
{
	if (req_w(cmd_req_word_four) != 0x0000 || !IS_NUM) {
		cmd_error_unknown();
		return;
	}
	uint8_t v = (uint8_t)cmd_reg_var_integers[1];
	switch (req_w(cmd_req_word_two)) {
	case 0x696E:
		if (ch_ok(BAS_FLOW_MAX_IN_CHANNEL)) { api_set_in_vol(CH, v); cmd_answer_ok(); }
		break;
	case 0x6F75:
		if (ch_ok(BAS_FLOW_MAX_OUT_CHANNEL)) { api_set_out_vol(CH, v); cmd_answer_ok(); }
		break;
#if BAS_MODUL_SRC43X2 > 0 || BAS_MODUL_CT7302 > 0
	case 0x7370:
		if (ch_ok(BAS_FLOW_MAX_SPDIF_CHANNEL)) { api_set_spdif_vol(CH, v); cmd_answer_ok(); }
		break;
#endif
	default: cmd_error_unknown();
	}
}

typedef uint16_t (*get_fn)(uint8_t ch);

static uint16_t g_in(uint8_t ch) { return api_get_in_vol(ch); }
static uint16_t g_out(uint8_t ch) { return api_get_out_vol(ch); }
static uint16_t g_spdif(uint8_t ch) { return api_get_spdif_vol(ch); }
static uint16_t g_mute(uint8_t ch) { return (api_get_out_mute_store() >> (ch - 1)) & 1; }
static uint16_t g_phase(uint8_t ch) { return (api_get_phase() >> (ch - 1)) & 1; }

/* "x.y.N" single value, "x.y" array, "x.y.*" one line per channel, "x.y.scale" */
static void get_channels(uint8_t max, get_fn f, const char *scale)
{
	if (req_w(cmd_req_word_four) != 0x0000) {
		cmd_error_unknown();
		return;
	}
	if (IS_NUM) {
		if (ch_ok(max)) {
			cmd_answer_header();
			out_u16(f(CH));
			out_crlf();
		}
		return;
	}
	switch (req_w(cmd_req_word_three)) {
	case 0x0000:
		cmd_answer_header();
		out_chr('[');
		for (uint8_t i = 1; i <= max; i++) {
			out_u16(f(i));
			if (i < max)
				out_chr(',');
		}
		out_chr(']');
		out_crlf();
		break;
	case 0x002A:
		REQ_B(8) = cmd_req_number;
		for (uint8_t i = 1; i <= max; i++) {
			REQ_B(7) = i;
			cmd_answer_header();
			out_u16(f(i));
			out_crlf();
		}
		break;
	case 0x7363:
		if (scale) {
			cmd_answer_header();
			out_str(scale);
			out_crlf();
			break;
		}
		/* fall through */
	default: cmd_error_unknown();
	}
}

void cmd_get_gain(void)
{
	switch (req_w(cmd_req_word_two)) {
	case 0x696E: get_channels(BAS_FLOW_MAX_AD_CHANNEL, g_in, BAS_ADC_SCALE); break;
	case 0x6F75: get_channels(BAS_FLOW_MAX_OUT_CHANNEL, g_out, BAS_DAC_SCALE); break;
#if BAS_MODUL_SRC43X2 > 0 || BAS_MODUL_CT7302 > 0
	case 0x7370: get_channels(BAS_FLOW_MAX_SPDIF_CHANNEL, g_spdif, BAS_SPDIF_SCALE); break;
#else
	case 0x7370: (void)g_spdif; break;  /* BASCOM: no answer without SPDIF module */
#endif
	default: cmd_error_unknown();
	}
}

/* --- mute / phase: one bit per output --- */

static uint8_t set_bit_value(uint8_t cur)
{
	uint8_t b = (uint8_t)(1u << (CH - 1));
	return cmd_reg_var_integers[1] > 0 ? (uint8_t)(cur | b) : (uint8_t)(cur & ~b);
}

static const uint16_t mute_event[cmd_req_words_max] = {0x0104, 0x6D75, 0x6F75, 0x0000, 0x0000};

void cmd_set_mute(void)
{
	if (req_w(cmd_req_word_four) != 0x0000 || req_w(cmd_req_word_two) != 0x6F75 || !IS_NUM) {
		cmd_error_unknown();
		return;
	}
	if (!ch_ok(BAS_FLOW_MAX_OUT_CHANNEL))
		return;
	uint8_t v = set_bit_value(api_get_out_mute_store());
	api_set_out_mute_store(v);
	cmd_answer_ok();
	char *p = cmd_req_value;
	*p++ = '[';
	for (uint8_t i = 1; i <= BAS_FLOW_MAX_OUT_CHANNEL; i++) {
		*p++ = (char)('0' + ((v >> (i - 1)) & 1));
		if (i < BAS_FLOW_MAX_OUT_CHANNEL)
			*p++ = ',';
	}
	*p++ = ']';
	*p = 0;
	cmd_load_header(mute_event);
	cmd_sendevent();
}

void cmd_get_mute(void)
{
	if (req_w(cmd_req_word_two) == 0x6F75)
		get_channels(BAS_FLOW_MAX_OUT_CHANNEL, g_mute, NULL);
	else
		cmd_error_unknown();
}

void cmd_set_phase(void)
{
	if (req_w(cmd_req_word_four) != 0x0000 || req_w(cmd_req_word_two) != 0x6F75 || !IS_NUM) {
		cmd_error_unknown();
		return;
	}
	if (!ch_ok(BAS_FLOW_MAX_OUT_CHANNEL))
		return;
	api_set_phase(set_bit_value(api_get_phase()));
	cmd_answer_ok();
}

void cmd_get_phase(void)
{
	if (req_w(cmd_req_word_two) == 0x6F75)
		get_channels(BAS_FLOW_MAX_OUT_CHANNEL, g_phase, NULL);
	else
		cmd_error_unknown();
}
