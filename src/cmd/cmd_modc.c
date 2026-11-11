/*
 * Port of libs/cmd/v20/common/subs/cmd_modc (version 20, modc_version_libs 20).
 *   set modc.N=[enable,type,FFFFFFFF,e1,e2,e3,e4,SSSSSSSS]  (flags/sets as 8 hex digits, 1 first)
 *   set modc.N.enable / .type / .events.K / .flags[.K] / .sets.K
 *   get modc.N, modc.N.*, modc.*, modc.N.enable/.type/.events[.K]/.flags[.K]/.sets[.K],
 *       modc.enable, modc.version.attributes[.aa|.ab|.ac|.ad]
 * Block 32 (max_modc) holds the start-up delays; its enable/type cannot be set, as in the original.
 * PORT: block number 0 is refused (the original uses index 0 of its arrays).
 * "get modc.enable" prints 0 for configured blocks and the enable value for empty ones, as
 * the original does (looks inverted, kept for the GUI).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "cmd.h"
#include "bas_data.h"
#include "flavour.h"
#include "flavour_data.h"
#include "modc.h"

#define N REQ_B(5)
#define K REQ_B(9)
#define K_IS_NUM (REQ_B(10) == cmd_req_number)

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

static void out_hex2(uint8_t b)
{
	static const char h[] = "0123456789ABCDEF";
	out_chr(h[b >> 4]);
	out_chr(h[b & 15]);
}

static void set_values(void)
{
	if (modc_current_moc >= MODC_N) {
		cmd_error_cmd_oor();
		return;
	}
	if (cmd_reg_var_count != 8) {
		cmd_error_unknown_para();
		return;
	}
	modc_set_enable((uint8_t)cmd_reg_var_integers[1]);
	modc_set_type((uint8_t)cmd_reg_var_integers[2]);
	uint32_t d = hexval(cmd_reg_var_strings[2]);
	for (uint8_t i = 1; i <= 4; i++)
		modc_set_flags(i, (uint8_t)(d >> (8 * (4 - i))));
	for (uint8_t i = 1; i <= 4; i++)
		modc_set_event(i, (uint8_t)cmd_reg_var_integers[3 + i]);
	d = hexval(cmd_reg_var_strings[7]);
	for (uint8_t i = 1; i <= 4; i++)
		modc_set_set(i, (uint8_t)(d >> (8 * (4 - i))));
	cmd_answer_ok();
}

void cmd_set_modc(void)
{
	uint8_t v = (uint8_t)cmd_reg_var_integers[1];
	if (REQ_B(6) != cmd_req_number) {
		cmd_error_unknown();
		return;
	}
	if (N < 1 || N > MODC_N) {
		cmd_error_cmd_oor();
		return;
	}
	modc_current_moc = N;
	switch (req_w(cmd_req_word_three)) {
	case 0x0000: set_values(); break;
	case 0x656E:  /* enable */
		if (req_w(cmd_req_word_four) != 0x0000) cmd_error_unknown();
		else if (modc_current_moc < MODC_N) { modc_set_enable(v); cmd_answer_ok(); }
		else cmd_error_cmd_oor();
		break;
	case 0x6576:  /* events.K */
		if (!K_IS_NUM) cmd_error_unknown();
		else if (K >= 1 && K <= 4) { modc_set_event(K, v); cmd_answer_ok(); }
		else cmd_error_cmd_oor();
		break;
	case 0x666C:  /* flags[.K] */
		if (!K_IS_NUM) { modc_set_flags(1, v); cmd_answer_ok(); }
		else if (K >= 1 && K <= 4) { modc_set_flags(K, v); cmd_answer_ok(); }
		else cmd_error_cmd_oor();
		break;
	case 0x7365:  /* sets.K */
		if (!K_IS_NUM) cmd_error_unknown();
		else if (K >= 1 && K <= 4) { modc_set_set(K, v); cmd_answer_ok(); }
		else cmd_error_cmd_oor();
		break;
	case 0x7479:  /* type */
		if (modc_current_moc < MODC_N) { modc_set_type(v); cmd_answer_ok(); }
		else cmd_error_cmd_oor();
		break;
	default: cmd_error_unknown();
	}
}

static void get_data(uint8_t n)
{
	cmd_answer_header();
	out_chr('[');
	out_u16(modc_enable[n]);
	out_chr(',');
	out_u16(modc_type[n]);
	out_chr(',');
	for (uint8_t i = 0; i < 4; i++)
		out_hex2(modc_flags[i * MODC_N + n]);
	out_chr(',');
	for (uint8_t i = 0; i < 4; i++) {
		out_u16(modc_events[i * MODC_N + n]);
		out_chr(',');
	}
	for (uint8_t i = 0; i < 4; i++)
		out_hex2(modc_sets[i * MODC_N + n]);
	out_chr(']');
	out_crlf();
}

static void get_asterisk(void)
{
	REQ_B(6) = cmd_req_number;
	for (uint8_t i = 1; i <= MODC_N; i++) {
		if (modc_type[i] > BAS_MODC_TYPE_EMPTY) {
			REQ_B(5) = i;
			get_data(i);
		}
	}
}

/* modc.N.<events|flags|sets>[.K] */
static void get_four(const uint8_t *arr)
{
	if (req_w(cmd_req_word_four) == 0x0000) {
		cmd_answer_header();
		out_chr('[');
		for (uint8_t i = 0; i < 4; i++) {
			out_u16(arr[i * MODC_N + N]);
			if (i < 3)
				out_chr(',');
		}
		out_chr(']');
		out_crlf();
	} else if (!K_IS_NUM) {
		cmd_error_unknown();
	} else if (K < 1 || K > 4) {
		cmd_error_cmd_oor();
	} else {
		cmd_answer_header();
		out_u16(arr[(K - 1) * MODC_N + N]);
		out_crlf();
	}
}

static void get_version_attr(void)
{
	uint8_t a;
	switch (req_w(cmd_req_word_four)) {
	case 0x0000: cmd_answer_header(); out_str(BAS_MODS_ATTRIBUTES); out_crlf(); return;
	case 0x6161: a = BAS_MODC_ATTRIBUTES_AA; break;
	case 0x6162: a = BAS_MODC_ATTRIBUTES_AB; break;
	case 0x6163: a = BAS_MODC_ATTRIBUTES_AC; break;
	case 0x6164: a = BAS_MODC_ATTRIBUTES_AD; break;
	default: cmd_error_unknown(); return;
	}
	cmd_two_character = req_w(cmd_req_word_four);
	cmd_answer_header();
	out_chr('[');
	for (uint8_t i = 0; i <= BAS_MODC_ATTRIBUTES_MAX; i++) {
		out_u16(BAS_LOOKUP_B(data_mods_versions, a + i));
		if (i < BAS_MODC_ATTRIBUTES_MAX)
			out_chr(',');
	}
	out_chr(']');
	out_crlf();
}

void cmd_get_modc(void)
{
	if (REQ_B(6) == cmd_req_number) {
		uint16_t w3 = req_w(cmd_req_word_three);
		if (w3 == 0x002A) {  /* as the original: modc.N.* lists all blocks */
			get_asterisk();
			return;
		}
		if (N < 1 || N > MODC_N) {
			cmd_error_cmd_oor();
			return;
		}
		switch (w3) {
		case 0x0000: get_data(N); break;
		case 0x656E:
			if (req_w(cmd_req_word_four) != 0x0000) cmd_error_unknown();
			else { cmd_answer_header(); out_u16(modc_enable[N]); out_crlf(); }
			break;
		case 0x6576: get_four(modc_events); break;
		case 0x666C: get_four(modc_flags); break;
		case 0x7365: get_four(modc_sets); break;
		case 0x7479: cmd_answer_header(); out_u16(modc_type[N]); out_crlf(); break;
		default: cmd_error_unknown();
		}
		return;
	}
	switch (req_w(cmd_req_word_two)) {
	case 0x002A: get_asterisk(); break;
	case 0x656E:
		if (req_w(cmd_req_word_three) != 0x0000) {
			cmd_error_unknown();
			break;
		}
		cmd_answer_header();
		out_chr('[');
		for (uint8_t i = 1; i <= MODC_N; i++) {
			out_u16(modc_type[i] > BAS_MODC_TYPE_EMPTY ? 0 : modc_enable[i]);
			if (i < MODC_N)
				out_chr(',');
		}
		out_chr(']');
		out_crlf();
		break;
	case 0x7665:  /* version.attributes */
		if (req_w(cmd_req_word_three) == 0x6174)
			get_version_attr();
		else
			cmd_error_unknown();
		break;
	default: cmd_error_unknown();
	}
}
