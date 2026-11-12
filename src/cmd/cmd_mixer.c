/*
 * Port of libs/cmd/v20/common/subs/cmd_mixer (version 20): mixer.P.C.G (P position 1..4,
 * C channel 1..10, G gain 1..10; data_flow_mix_cmd2array maps to the storage, 0xFF = input
 * mux of the position), mixer.position, mixer.* and mixer.scale.
 * PORT: all three path elements must be numbers; the original lets a missing element
 * (word 0x0000) through, e.g. "mixer.1.2" sets the mux of position 1.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "bqramp.h"
#include "cmd.h"
#include "api.h"
#include "bas_data.h"
#include "flavour.h"
#include "flavour_data.h"
#include "store.h"

#define SV(base, i) storage_non_biquadvalues[(base) + (i) - 1]
#define store_mix_position storage_non_biquadvalues[BAS_STORE_MIX_POSITION_BASE]

static uint8_t num(uint8_t lo, uint8_t max)
{
	return REQ_B(lo + 1) == cmd_req_number && REQ_B(lo) >= 1 && REQ_B(lo) <= max;
}

static uint8_t cmd2array(uint8_t c, uint8_t g)
{
	return BAS_LOOKUP_B(data_flow_mix_cmd2array, (c - 1) * BAS_FLOW_MIX_MAX_CHANNELS + g);
}

static void cmd_set_mixer_value(void)
{
	if (!num(5, BAS_FLOW_MIX_MAX_POSITIONS) || !num(7, BAS_FLOW_MIX_MAX_CHANNELS) ||
	    !num(9, BAS_FLOW_MIX_MAX_GAINS)) {
		cmd_error_cmd_oor();
		return;
	}
	uint8_t b = cmd2array(REQ_B(7), REQ_B(9));
	uint8_t a = REQ_B(5);
	/* PORT (#50): with clickless on a gain change ramps the row instead of muting */
	uint8_t ramp = bqramp_enabled() && b != 0x00 && b != 0xFF && a == store_mix_position;
	if (a == store_mix_position && !ramp)
		api_set_out_mute_change();
	switch (b) {
	case 0x00:  /* nothing */
		cmd_error_cmd_oor();
		break;
	case 0xFF:  /* mux */
		SV(BAS_STORAGE_MUX_BASE, a) = (uint8_t)cmd_reg_var_integers[1];
		cmd_answer_ok();
		storage_flags++;
		api_publish_in_input();  /* PORT: events 45..48 (#23) */
		break;
	default:
		if (ramp)
			api_mix_ramp_request(BAS_LOOKUP_B(data_flow_mix_can_change, b));
		else if (a == store_mix_position)  /* (original also reads can_change(0xFF) for the mux, unused) */
			store_mix_changed |= 1UL << BAS_LOOKUP_B(data_flow_mix_can_change, b);
		SV(BAS_STORAGE_MIX_BASE, (uint16_t)((a - 1) * BAS_FLOW_MIX_MAX_POS_FRAME + b)) = (uint8_t)cmd_reg_var_integers[1];
		cmd_answer_ok();
		storage_flags++;
	}
}

static void cmd_set_mixer_position(void)
{
	int16_t v = cmd_reg_var_integers[1];
	if (req_w(cmd_req_word_three) != 0x0000) {
		cmd_error_unknown();
	} else if (v < 1 || v > BAS_FLOW_MIX_MAX_POSITIONS) {
		cmd_error_cmd_oor();
	} else {
		cmd_answer_ok();
		api_set_mix_position((uint8_t)v);
		storage_flags++;
	}
}

static const uint16_t input_event[cmd_req_words_max] = {0x0104, 0x6D69, 0x696E, 0x0000, 0x0000};

/* PORT extension (#23): mixer.input = input (mux) of the active position */
static void cmd_set_mixer_input(void)
{
	if (req_w(cmd_req_word_three) != 0x0000) {
		cmd_error_unknown();
		return;
	}
	int16_t v = cmd_reg_var_integers[1];
	if (v < 1 || v > 255 || !api_set_in_input((uint8_t)v)) {
		cmd_error_cmd_oor();
		return;
	}
	cmd_answer_ok();
	uint8_t i = 0;
	if (v >= 10) cmd_req_value[i++] = (char)('0' + v / 10 % 10);
	cmd_req_value[i++] = (char)('0' + v % 10);
	cmd_req_value[i] = 0;
	cmd_load_header(input_event);
	cmd_sendevent();
}

void cmd_set_mixer(void)
{
	switch (req_w(cmd_req_word_two)) {
	case 0x706F: cmd_set_mixer_position(); break;
	case 0x696E: cmd_set_mixer_input(); break;
	default: cmd_set_mixer_value();
	}
}

static const uint16_t mix_answer[cmd_req_words_max] = {0x0102, 0x6D69, 0x2300, 0x2300, 0x2300};

static void cmd_get_mixer_asterisk(void)
{
	cmd_load_header(mix_answer);
	for (uint8_t i = 1; i <= BAS_STORE_MIX_POSITIONS; i++) {
		REQ_B(5) = i;
		for (uint8_t ii = 1; ii <= BAS_FLOW_MIX_MAX_CHANNELS; ii++) {
			REQ_B(7) = ii;
			for (uint8_t iii = 1; iii <= BAS_FLOW_MIX_MAX_GAINS; iii++) {
				REQ_B(9) = iii;
				uint8_t b = cmd2array(ii, iii);
				if (b == 0x00)
					continue;
				cmd_answer_header();
				if (b == 0xFF)
					out_u16(SV(BAS_STORAGE_MUX_BASE, i));
				else
					out_u16(SV(BAS_STORAGE_MIX_BASE, (uint16_t)((i - 1) * BAS_FLOW_MIX_MAX_POS_FRAME + b)));
				out_crlf();
			}
		}
	}
}

void cmd_get_mixer(void)
{
	switch (req_w(cmd_req_word_two)) {
	case 0x002A: cmd_get_mixer_asterisk(); break;
	case 0x706F: cmd_answer_header(); out_u16(store_mix_position); out_crlf(); break;
	case 0x7363: cmd_answer_header(); out_str(BAS_CMD_MIX_SCALE); out_crlf(); break;
	case 0x696E:  /* input (#23); input.max: highest valid input */
		if (req_w(cmd_req_word_three) == 0x6D61) {
			cmd_answer_header(); out_u16(api_in_input_max()); out_crlf();
		} else if (req_w(cmd_req_word_three) == 0x0000) {
			cmd_answer_header(); out_u16(api_get_in_input()); out_crlf();
		} else {
			cmd_error_unknown();
		}
		break;
	default: cmd_error_unknown();
	}
}
