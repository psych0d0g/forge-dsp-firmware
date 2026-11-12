/*
 * Port of libs/cmd/v20/common/subs/cmd_biquad (version 20).
 *   set biquad.CH.N=[type,f,gain,q,b(,flags)]        store the values (bqtv) only
 *   set biquad.CH.N.calc=[...]                       store values, compute, store and send coefficients
 *   set biquad.CH.N.raw=["A2","A1","B2","B1","B0"]   hex 5.23 coefficients, store and send
 *   get biquad / biquad.* / biquad.CH / biquad.CH.N  stored values [type,f,gain,q,b,flags]
 * Old format (5 values): q in 1/10, gain in 1/10 dB; new format (6 values): q in 1/1000.
 * PORT: biquad.CH.N.raw splits the quoted values itself (broken in the original, see below).
 * PORT: set/get of stored values are refused while no preset is loaded (store_preset 0xFF).
 * PORT: range tables are applied to values 2..6 only (the original reads values 7/8 beyond
 * data_ranges_biquad_*); biquad.CH.0 is rejected (the original addresses the previous channel's
 * last biquad). double -> long truncates (BASCOM rounding still to be checked on hardware, #4).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "cmd.h"
#include "api.h"
#include "bas_data.h"
#include "biquad.h"
#include "bqramp.h"
#include "dsp.h"
#include "flavour.h"
#include "dsp_pool.h"
#include "flavour_data.h"
#include "i2c.h"
#include "store.h"

#define ISSINGLE(c) (cmd_reg_var_states[c] & (1 << cmd_reg_state_isSingle))

static int32_t lookup_l(const uint8_t *t, uint8_t i)
{
	uint16_t a = (uint16_t)i * 4;
	return (int32_t)((uint32_t)bas_data_byte(&t[a]) | (uint32_t)bas_data_byte(&t[a + 1]) << 8 |
	                 (uint32_t)bas_data_byte(&t[a + 2]) << 16 | (uint32_t)bas_data_byte(&t[a + 3]) << 24);
}

static int32_t var_l(uint8_t c)  /* single -> long, else integer */
{
	return ISSINGLE(c) ? (int32_t)cmd_reg_var_singles[c] : cmd_reg_var_integers[c];
}

static void chkvalues(void)
{
	uint8_t n = cmd_reg_var_count < 6 ? cmd_reg_var_count : 6;
	for (uint8_t c = 2; c <= n; c++) {
		if (ISSINGLE(c)) {
			double lo = lookup_l(data_ranges_biquad_min_L, c) / 10.0;
			double hi = lookup_l(data_ranges_biquad_max_L, c) / 10.0;
			if (cmd_reg_var_singles[c] < lo) cmd_reg_var_singles[c] = lo;
			if (cmd_reg_var_singles[c] > hi) cmd_reg_var_singles[c] = hi;
		} else {
			int16_t lo = (int16_t)BAS_LOOKUP_W(data_ranges_biquad_min_I, c);
			int16_t hi = (int16_t)BAS_LOOKUP_W(data_ranges_biquad_max_I, c);
			if (cmd_reg_var_integers[c] < lo) cmd_reg_var_integers[c] = lo;
			if (cmd_reg_var_integers[c] > hi) cmd_reg_var_integers[c] = hi;
		}
	}
}

static void put16(uint8_t at, uint16_t v)
{
	buffer_bytes[at] = v & 0xFF;
	buffer_bytes[at + 1] = v >> 8;
}

/* bqtv frame: bytes(1) type, bytes(2) flags, words(2) f, integers(3) gain, words(4) q, words(5) b */
static void copyvalues(void)
{
	buffer_bytes[1] = (uint8_t)var_l(1);
	put16(3, (uint16_t)var_l(2));
	put16(5, (uint16_t)(ISSINGLE(3) ? (int32_t)(cmd_reg_var_singles[3] * 10) : cmd_reg_var_integers[3]));
	if (ISSINGLE(4))
		put16(7, (uint16_t)(int32_t)(cmd_reg_var_singles[4] * 1000));
	else
		put16(7, (uint16_t)(cmd_reg_var_count > 5 ? cmd_reg_var_integers[4] : cmd_reg_var_integers[4] * 100));
	put16(9, (uint16_t)var_l(5));
	buffer_bytes[2] = BAS_DSP_BQ_FLAGS_DEFAULTS;
	if (cmd_reg_var_count > 5)
		buffer_bytes[2] = (uint8_t)var_l(6);
}

static struct bq_params calc_in;

static void calc(void)
{
	dsp_bq_cache_type = (uint8_t)var_l(1);
	uint8_t a = BAS_DSP_BQ_FLAGS_DEFAULTS;
	if (cmd_reg_var_count > 5)
		a = (uint8_t)var_l(6);
	if (!(a & (1u << BAS_DSP_BQ_FLAGS_ALL_PASS)))
		dsp_bq_cache_type = BAS_DSP_BQTYPE_ALL_PASS;
	for (uint8_t c = 2; c <= 5; c++) {  /* f, gain, q, b */
		uint8_t i = BAS_LOOKUP_B(data_dsp_cache_values, c);
		switch (c) {
		case 3:
			dsp_bq_cache_values[i] = ISSINGLE(c) ? cmd_reg_var_singles[c] : cmd_reg_var_integers[c] / 10.0;
			break;
		case 4:
			if (ISSINGLE(c))
				dsp_bq_cache_values[i] = cmd_reg_var_singles[c];
			else
				dsp_bq_cache_values[i] = cmd_reg_var_integers[c] / (cmd_reg_var_count > 5 ? 1000.0 : 10.0);
			break;
		default:
			dsp_bq_cache_values[i] = (double)var_l(c);
		}
	}
	calc_in.type = dsp_bq_cache_type;  /* PORT (#44): parameters before the type function changes them */
	calc_in.f = dsp_bq_cache_values[BAS_DSP_BQ_CACHE_VALUE_F];
	calc_in.g = dsp_bq_cache_values[BAS_DSP_BQ_CACHE_VALUE_GAIN];
	calc_in.q = dsp_bq_cache_values[BAS_DSP_BQ_CACHE_VALUE_Q];
	calc_in.b = dsp_bq_cache_values[BAS_DSP_BQ_CACHE_VALUE_B];
	dsp_bq_calc_type(dsp_bq_cache_type);
}

static void raw_to_buffer(const uint32_t raw[6])
{
	for (uint8_t i = 1; i <= 5; i++)
		for (uint8_t k = 0; k < 4; k++)
			buffer_bytes[(i - 1) * 4 + 1 + k] = (uint8_t)(raw[i] >> (8 * k));
}

static uint32_t hexval(const char *s)
{
	uint32_t v = 0;
	for (; *s; s++) {
		uint8_t d;
		if (*s >= '0' && *s <= '9') d = (uint8_t)(*s - '0');
		else if (*s >= 'a' && *s <= 'f') d = (uint8_t)(*s - 'a' + 10);
		else if (*s >= 'A' && *s <= 'F') d = (uint8_t)(*s - 'A' + 10);
		else break;
		v = v << 4 | d;
	}
	return v;
}

static void set_biquad_calc(const uint32_t raw[6])
{
	dsp_calc_core();
	dsp_calc_reg();
	dsp_set_biquad(raw);
}

static void set_bq_ch(void)
{
	uint8_t limits = flow_bq_limit(REQ_B(5));
	if (REQ_B(8) != cmd_req_number) {
		cmd_error_unknown();
		return;
	}
	if (REQ_B(7) < 1 || REQ_B(7) > limits) {
		cmd_error_cmd_oor();
		return;
	}
	store_bq_nr = (uint16_t)(flow_bq_offset(REQ_B(5)) + REQ_B(7));
	dsp_bq_virt_nr = store_bq_nr;
	uint32_t raw[6] = {0};
	switch (req_w(cmd_req_word_four)) {
	case 0x0000:
		chkvalues();
		copyvalues();
		store_save_bqtv();
		cmd_answer_ok();
		break;
	case 0x6361: {  /* calc */
		struct bq_params old = {0}, to;
		(void)to;
		uint8_t fade = bqramp_enabled();
		if (fade) {  /* PORT (#44): parameters before this change, from the preset storage */
			store_load_bqtv();
			old.type = (buffer_bytes[2] & (1u << BAS_DSP_BQ_FLAGS_ALL_PASS)) ? buffer_bytes[1] : BAS_DSP_BQTYPE_ALL_PASS;
			old.f = (uint16_t)(buffer_bytes[3] | buffer_bytes[4] << 8);
			old.g = (int16_t)(buffer_bytes[5] | buffer_bytes[6] << 8) / 10.0;
			old.q = (uint16_t)(buffer_bytes[7] | buffer_bytes[8] << 8) / 1000.0;
			old.b = (uint16_t)(buffer_bytes[9] | buffer_bytes[10] << 8);
		}
		chkvalues();
		copyvalues();
		store_save_bqtv();
		calc();
		to = calc_in;
		dsp_bq_calcto5coeff();
		dsp_bq_convert2raw(raw);
		raw_to_buffer(raw);
		store_save_bq();
		if (!fade || !bqramp_start(store_bq_nr, &old, &to)) {
			if (fade)
				api_set_out_mute_change();  /* cannot be faded: mute as the original */
			dsp_bq_virt_nr = store_bq_nr;
			set_biquad_calc(raw);
		}
		cmd_answer_ok();
		break;
	}
	case 0x7261:  /* raw A2 A1 B2 B1 B0 */
		if ((cmd_reg_var_state & (1 << cmd_reg_state_isArray)) &&
		    (cmd_reg_var_state & (1 << cmd_reg_state_isString))) {
			/* PORT: the original never splits quoted values (cmd_parse_values_to_vars returns
			 * early for strings) and uses the strings of the previous command; split here */
			const char *p = cmd_req_value;
			for (uint8_t i = 1; i <= 5; i++) {
				raw[i] = hexval(p);
				while (*p && *p != ',')
					p++;
				if (*p == ',')
					p++;
			}
			raw_to_buffer(raw);
			store_save_bq();
			bqramp_cancel(store_bq_nr);  /* PORT (#44): raw values are written as they are */
			set_biquad_calc(raw);
			cmd_answer_ok();
		} else {
			cmd_error_unknown();
		}
		break;
	default: cmd_error_unknown();
	}
}

/* PORT extension (#44): biquad.clickless = 0/1, fade biquad.CH.N.calc instead of muting */
static uint8_t clickless_cmd(uint8_t set)
{
	if (req_w(cmd_req_word_two) != 0x636C || req_w(cmd_req_word_three) != 0x0000)
		return 0;
	if (set) {
		if (cmd_reg_var_integers[1] != 0 && cmd_reg_var_integers[1] != 1) {
			cmd_error_cmd_oor();
		} else {
			bqramp_set_enabled((uint8_t)cmd_reg_var_integers[1]);
			cmd_answer_ok();
		}
	} else {
		cmd_answer_header();
		out_u16(bqramp_enabled());
		out_crlf();
	}
	return 1;
}

void cmd_set_biquad(void)
{
	if (clickless_cmd(1))
		return;
	if (store_preset >= BAS_MODUL_MAX_PRESET) {  /* PORT: no preset loaded, no storage address */
		cmd_error_cmd_oor();
	} else if (REQ_B(6) != cmd_req_number) {
		cmd_error_unknown();
	} else if (REQ_B(5) > BAS_FLOW_MAX_CHANNELS || flow_bq_limit(REQ_B(5)) == 0) {
		cmd_error_cmd_oor();
	} else {
		/* PORT (#44): with clickless on, calc decides itself whether it has to mute */
		if (!(bqramp_enabled() && req_w(cmd_req_word_four) == 0x6361))
			api_set_out_mute_change();
		set_bq_ch();
	}
}

static void get_bq_ch_data(void)
{
	cmd_answer_header();
	out_chr('[');
	out_u16(buffer_bytes[1]);
	out_chr(',');
	out_u16((uint16_t)(buffer_bytes[3] | buffer_bytes[4] << 8));
	out_chr(',');
	out_i16((int16_t)(buffer_bytes[5] | buffer_bytes[6] << 8));
	out_chr(',');
	out_u16((uint16_t)(buffer_bytes[7] | buffer_bytes[8] << 8));
	out_chr(',');
	out_u16((uint16_t)(buffer_bytes[9] | buffer_bytes[10] << 8));
	out_chr(',');
	out_u16(buffer_bytes[2]);
	out_chr(']');
	out_crlf();
}

static void get_bq_ch(uint8_t ch)
{
	uint8_t limits = flow_bq_limit(ch);
	uint16_t base = flow_bq_offset(ch);
	if (REQ_B(8) == cmd_req_number) {
		if (REQ_B(7) < 1 || REQ_B(7) > limits) {
			cmd_error_cmd_oor();
		} else {
			store_bq_nr = (uint16_t)(base + REQ_B(7));
			store_load_bqtv();
			get_bq_ch_data();
		}
		return;
	}
	uint16_t w = req_w(cmd_req_word_three);
	if (w != 0x0000 && w != 0x002A) {
		cmd_error_unknown();
		return;
	}
	REQ_B(8) = cmd_req_number;
	for (uint8_t i = 1; i <= limits; i++) {
		REQ_B(7) = i;
		store_bq_nr = (uint16_t)(base + i);
		store_load_bqtv();
		get_bq_ch_data();
	}
	req_w_set(cmd_req_word_three, w);
}

void cmd_get_biquad(void)
{
	if (clickless_cmd(0))
		return;
	if (req_w(cmd_req_word_four) != 0x0000) {
		cmd_error_unknown();
		return;
	}
	if (REQ_B(6) == cmd_req_number) {
		uint8_t i = REQ_B(5);
		if (store_preset >= BAS_MODUL_MAX_PRESET) {  /* PORT: no preset loaded */
			cmd_error_cmd_oor();
			return;
		}
		if (i > BAS_FLOW_MAX_CHANNELS || flow_bq_limit(i) == 0)
			cmd_error_cmd_oor();
		else
			get_bq_ch(i);
		return;
	}
	switch (req_w(cmd_req_word_two)) {
	case 0x0000:  /* biquad counts per channel */
		cmd_answer_header();
		out_chr('[');
		for (uint8_t i = 1; i <= BAS_FLOW_MAX_CHANNELS_USED; i++) {
			out_u16(flow_bq_limit(i));
			if (i < BAS_FLOW_MAX_CHANNELS_USED)
				out_chr(',');
		}
		out_chr(']');
		out_crlf();
		break;
	case 0x002A:
		if (store_preset >= BAS_MODUL_MAX_PRESET) {
			cmd_error_cmd_oor();
			break;
		}
		REQ_B(6) = cmd_req_number;
		for (uint8_t i = 1; i <= BAS_FLOW_MAX_CHANNELS; i++) {
			if (flow_bq_limit(i) > 0) {
				REQ_B(5) = i;
				get_bq_ch(i);
			}
		}
		break;
	default: cmd_error_unknown();
	}
}
