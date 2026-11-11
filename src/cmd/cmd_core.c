/*
 * Port of libs/cmd/v20/common/functions.bas (parser, answer header, errors, dispatch).
 * Kept close to the BASCOM source, including its quirks, so that answers stay
 * byte-identical; deviations are marked "PORT:".
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include <stdlib.h>
#include <string.h>
#include "cmd.h"
#include "iface.h"
#include "flavour.h"

uint8_t cmd_mem[cmd_req_bytes_max + cmd_req_inputs_max];
char cmd_req_value[cmd_req_value_size + 2];
char cmd_req_chksum[5];
uint8_t cmd_req_chksum_value;
uint8_t cmd_req_chksum_state;
uint16_t cmd_two_character;

uint8_t cmd_reg_var_count;
char cmd_reg_var_strings[cmd_reg_var_bytes_split][cmd_reg_var_string_len + 1];
int16_t cmd_reg_var_integers[cmd_reg_var_bytes_split + 1];
double cmd_reg_var_singles[cmd_reg_var_bytes_split + 1];
uint8_t cmd_reg_var_state;
uint8_t cmd_reg_var_states[cmd_reg_var_bytes_split + 1];

#define com_chr_lf    0x0A
#define com_chr_cr    0x0D
#define com_chr_space 0x20
#define com_chr_quota '"'
#define com_chr_dot   0x2E
#define com_chr_semi  0x3B
#define com_chr_equal 0x3D

uint16_t req_w(uint8_t n)
{
	uint16_t o = (uint16_t)(n - 1) * 2;
	if ((size_t)o + 1 >= sizeof cmd_mem)
		return 0;
	return cmd_mem[o] | (uint16_t)cmd_mem[o + 1] << 8;
}

void req_w_set(uint8_t n, uint16_t v)
{
	uint16_t o = (uint16_t)(n - 1) * 2;
	if ((size_t)o + 1 >= sizeof cmd_mem)
		return;
	cmd_mem[o] = v & 0xFF;
	cmd_mem[o + 1] = v >> 8;
}

const char *req_input_str(uint8_t n)
{
	/* string * 15 at cmd_req_inputs((n-1)*16+1); a full 16-char name has no
	 * terminator and runs into the next one, as in BASCOM (inputs end with 0 bytes) */
	return (const char *)&REQ_INPUT((uint16_t)(n - 1) * cmd_req_input_max + 1);
}

uint16_t bas_crc16(const uint8_t *p, uint16_t len)
{
	uint16_t crc = 0;
	while (len--) {
		crc ^= (uint16_t)*p++ << 8;
		for (uint8_t i = 0; i < 8; i++)
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
	}
	return crc;
}

void cmd_default_clear_req(void)
{
	for (uint8_t i = 1; i <= cmd_req_bytes_max; i++)
		REQ_B(i) = cmd_reg_unset;
	for (uint8_t i = 1; i <= cmd_req_inputs_max; i++)
		REQ_INPUT(i) = 0x00;
	cmd_req_value[0] = 0;
	cmd_req_chksum[0] = 0;
	cmd_req_chksum_state = cmd_chksum_state_empty;
}

static uint8_t cmd_find_next_arg(const uint8_t *buffer, uint8_t size, uint8_t count, uint8_t nextcode)
{
	uint8_t result;
	for (result = count; result <= size; result++) {
		uint8_t c = buffer[result];
		if (c == com_chr_lf || c == com_chr_cr || c == com_chr_equal || c == com_chr_semi)
			break;
		if ((c == com_chr_space || c == com_chr_dot) && nextcode != cmd_state_Value_begin)
			break;
	}
	return result - 1;
}

static uint8_t cmd_parse_for_cmd(uint8_t c)
{
	switch (c) {
	case cmd_short_error: return cmd_type_error;
	case cmd_short_info:  return cmd_type_info;
	case cmd_short_set:   return cmd_type_set;
	case cmd_short_get:   return cmd_type_get;
	case 0x67:            return cmd_type_get;   /* g */
	case 0x69:            return cmd_type_info;  /* i */
	case 0x73:            return cmd_type_set;   /* s */
	default:              return cmd_type_nothing;
	}
}

static uint8_t cmd_parse_for_target(const uint8_t *buffer, uint8_t size, uint8_t count)
{
	uint8_t result = cmd_target_nothing;
	for (uint8_t i = count; i <= size; i++) {
		uint8_t c = buffer[i];
		if (c >= 0x31 && c <= 0x36) {          /* only '1'..'6', as BASCOM */
			result = (uint8_t)(result * 10);
			result = (uint8_t)(result + (c - 0x30));
		} else if (c == 0x61) {
			return cmd_target_all;
		} else if (c == 0x6D) {
			return cmd_target_master;
		} else if (c == 0x73) {
			return cmd_target_slave;
		} else if (c == 0x74) {
			return cmd_target_this;
		} else {
			break;
		}
	}
	return result;
}

static int is_alnum(uint8_t c)
{
	return (c >= 0x30 && c <= 0x39) || (c >= 0x41 && c <= 0x5A) || (c >= 0x61 && c <= 0x7A);
}

static void cmd_parse_for_inputs(const uint8_t *buffer, uint8_t size, uint8_t count, uint8_t reg)
{
	reg--;
	uint8_t a = (uint8_t)(reg * cmd_req_input_max);
	uint8_t b = 0;
	for (uint8_t i = count; i <= size; i++) {
		a++;
		b++;
		if (b > cmd_req_input_max)
			break;
		if (!is_alnum(buffer[i]))
			break;
		REQ_INPUT(a) = buffer[i];
	}
}

static uint16_t cmd_parse_for_register(const uint8_t *buffer, uint8_t size, uint8_t count)
{
	uint16_t result = 0;
	uint8_t a = 0;
	uint8_t i;
	for (i = count; i <= size; i++) {
		uint8_t c = buffer[i];
		if (c == 0x2A)
			a = 3;
		else if (c >= 0x30 && c <= 0x39)
			a = 1;
		else if (c >= 0x41 && c <= 0x5A)
			a = 2;
		else if (c >= 0x61 && c <= 0x7A)
			a = 3;
		else
			break;
	}
	i--;
	/* the class of the LAST character decides, as in BASCOM */
	switch (a) {
	case 1:
		for (uint8_t ii = count; ii <= i; ii++) {
			if (ii > count)
				result = (uint16_t)(result * 10);
			uint8_t b = (uint8_t)(buffer[ii] - 0x30);
			result = (uint16_t)(result + b);
		}
		result = (uint16_t)(result + 0x2300);  /* '#' (cmd_req_number) marks a number */
		break;
	case 2:
		result = (uint16_t)(result + buffer[count] + 0x20);
		if (i > count) {
			result <<= 8;
			result = (uint16_t)(result + buffer[count + 1] + 0x20);
		}
		break;
	case 3:
		result = (uint16_t)(result + buffer[count]);
		if (i > count) {
			result <<= 8;
			result = (uint16_t)(result + buffer[count + 1]);
		}
		break;
	}
	return result;
}

static void cmd_fillup_register(void)
{
	switch (req_w(cmd_req_word_one)) {
	case 0x0070: req_w_set(cmd_req_word_one, 0x7072); break;  /* preset */
	case 0x0073: req_w_set(cmd_req_word_one, 0x7365); break;  /* select */
	}
	switch (req_w(cmd_req_word_two)) {
	case 0x0067: req_w_set(cmd_req_word_two, 0x6775); break;  /* system.gui */
	case 0x0069: req_w_set(cmd_req_word_two, 0x696E); break;  /* system.info */
	}
	switch (req_w(cmd_req_word_three)) {
	case 0x0070: req_w_set(cmd_req_word_three, 0x7061); break;  /* system.gui.panes */
	}
}

static uint8_t hexnib(uint8_t c, uint8_t *ok)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	*ok = 0;
	return 0;
}

/* BASCOM hexval() on a 2-char string; PORT: stops at the first non-hex char */
static uint8_t bas_hexval2(const char *s)
{
	uint8_t v = 0, ok = 1;
	for (uint8_t i = 0; i < 2 && s[i]; i++) {
		uint8_t n = hexnib((uint8_t)s[i], &ok);
		if (!ok)
			break;
		v = (uint8_t)(v << 4 | n);
	}
	return v;
}

void cmd_default_parse(const uint8_t *buffer, uint8_t size)
{
	uint8_t nextcode = cmd_state_Cmd;
	uint8_t reg = 1;
	uint8_t registerbegin = 0;
	uint8_t a, b;

	cmd_default_clear_req();
	for (uint8_t count = 1; count <= size; count++) {
		uint8_t c = buffer[count];
		if (c == com_chr_lf || c == com_chr_cr)
			break;
		if (c == com_chr_space) {
			switch (nextcode) {
			case cmd_state_Target:
				REQ_B(cmd_req_receiver) = cmd_target_device0;
				nextcode = cmd_state_Register;
				break;
			case cmd_state_Dot:    nextcode = cmd_state_Value_begin; break;
			case cmd_state_Spacer: nextcode = cmd_state_Register; break;
			}
			continue;
		}
		if (c == com_chr_dot) {
			if (nextcode != cmd_state_Target)
				nextcode = cmd_state_Register;
			continue;
		}
		if (c == com_chr_equal) {
			nextcode = cmd_state_Value_begin;
			continue;
		}
		switch (nextcode) {
		case cmd_state_Cmd:
			a = cmd_parse_for_cmd(c);
			if (a == cmd_type_nothing)
				goto done;
			REQ_B(cmd_req_type) = a;
			nextcode = cmd_state_Target;
			count = cmd_find_next_arg(buffer, size, count, nextcode);
			break;
		case cmd_state_Target:
			a = cmd_parse_for_target(buffer, size, count);
			REQ_B(cmd_req_receiver) = a;
			if (a >= cmd_target_device1 && a <= cmd_target_device7) {
				count = size;  /* other device: skip registers, values and chksum */
			} else {
				nextcode = cmd_state_Register;
				count = cmd_find_next_arg(buffer, size, count, nextcode);
			}
			break;
		case cmd_state_Register:
			if (reg == 1)
				registerbegin = count;
			reg++;
			if (reg < 6) {
				cmd_parse_for_inputs(buffer, size, count, reg);
				req_w_set(reg, cmd_parse_for_register(buffer, size, count));
			}
			nextcode = req_w(reg) > 0 ? cmd_state_Dot : cmd_state_Equal;
			count = cmd_find_next_arg(buffer, size, count, nextcode);
			break;
		case cmd_state_Value_begin:
			a = count;
			count = cmd_find_next_arg(buffer, size, count, nextcode);
			b = 0;
			for (uint8_t ii = a; ii <= count && b < cmd_req_value_size; ii++)
				cmd_req_value[b++] = (char)buffer[ii];
			cmd_req_value[b] = 0;
			nextcode = cmd_state_ChkSum;
			count = cmd_find_next_arg(buffer, size, count, nextcode);
			break;
		case cmd_state_ChkSum:
			a = count;
			for (uint8_t ii = 0; ii < 2; ii++) {
				a++;
				cmd_req_chksum[ii] = (char)buffer[a];
			}
			cmd_req_chksum[2] = 0;
			cmd_req_chksum_state = cmd_chksum_state_err;
			cmd_req_chksum_value = bas_hexval2(cmd_req_chksum);
			a = (uint8_t)(count - 1);
			b = 0;
			for (uint8_t ii = registerbegin; ii <= a; ii++)
				b = (uint8_t)(b + buffer[ii]);
			if (b == cmd_req_chksum_value)
				cmd_req_chksum_state = cmd_chksum_state_ok;
			nextcode = cmd_state_Spacer;
			break;
		}
	}
done:
	cmd_fillup_register();
}

/* BASCOM val(): leading blanks, sign, decimal or &H / &B; PORT: via strtod/strtol */
static double bas_val(const char *s)
{
	while (*s == ' ')
		s++;
	if (s[0] == '&' && (s[1] == 'H' || s[1] == 'h'))
		return (double)strtol(s + 2, NULL, 16);
	if (s[0] == '&' && (s[1] == 'B' || s[1] == 'b'))
		return (double)strtol(s + 2, NULL, 2);
	return strtod(s, NULL);
}

static void delchars(char *s, char c)
{
	char *d = s;
	for (; *s; s++)
		if (*s != c)
			*d++ = *s;
	*d = 0;
}

void cmd_parse_values_to_vars(void)
{
	cmd_reg_var_state = 0;
	for (uint8_t i = 1; i <= cmd_reg_var_bytes_split; i++)
		cmd_reg_var_states[i] = 0;
	if (strchr(cmd_req_value, com_chr_quota)) {
		cmd_reg_var_state |= 1 << cmd_reg_state_isString;
		delchars(cmd_req_value, com_chr_quota);
	}
	if (strchr(cmd_req_value, '[')) {
		cmd_reg_var_state |= 1 << cmd_reg_state_isArray;
		delchars(cmd_req_value, '[');
		delchars(cmd_req_value, ']');
	}
	if (cmd_reg_var_state & (1 << cmd_reg_state_isString))
		return;

	/* split(cmd_req_value, cmd_reg_var_strings(1), ","); PORT: parts are cut to 23 chars
	 * (BASCOM would overwrite the next string), an empty value gives 0 parts */
	uint8_t n = 0;
	const char *p = cmd_req_value;
	if (*p) {
		for (;;) {
			const char *e = strchr(p, ',');
			size_t l = e ? (size_t)(e - p) : strlen(p);
			if (n < cmd_reg_var_bytes_split) {
				if (l > cmd_reg_var_string_len)
					l = cmd_reg_var_string_len;
				memcpy(cmd_reg_var_strings[n], p, l);
				cmd_reg_var_strings[n][l] = 0;
			}
			n++;
			if (!e)
				break;
			p = e + 1;
		}
	}
	cmd_reg_var_count = n > cmd_reg_var_bytes_split ? cmd_reg_var_bytes_split : n;

	for (uint8_t i = 1; i <= cmd_reg_var_count; i++) {
		const char *s = cmd_reg_var_strings[i - 1];
		if (strchr(s, '.')) {
			cmd_reg_var_states[i] |= 1 << cmd_reg_state_isSingle;
			cmd_reg_var_singles[i] = bas_val(s);
			continue;
		}
		uint16_t w = bas_crc16((const uint8_t *)s, (uint8_t)strlen(s));
		switch (w) {
		case 0x1AB6: /* off */
		case 0x3653: /* 0 */
		case 0x743C: /* false */
			cmd_reg_var_states[i] |= 1 << cmd_reg_state_isBoolean;
			cmd_reg_var_integers[i] = 0;
			break;
		case 0x2672: /* 1 */
		case 0x967C: /* on */
		case 0xF543: /* true */
			cmd_reg_var_states[i] |= 1 << cmd_reg_state_isBoolean;
			cmd_reg_var_integers[i] = 1;
			break;
		default:
			cmd_reg_var_integers[i] = (int16_t)bas_val(s);
		}
	}
}

void cmd_answer_ok(void)
{
	REQ_B(cmd_req_type) = cmd_type_info;
	cmd_answer_header();
	out_str(" ok");
	out_crlf();
}

static void error_line(const char *msg)
{
	REQ_B(cmd_req_type) = cmd_type_error;
	cmd_answer_header();
	out_str(msg);
	out_crlf();
}

void cmd_error_unknown(void)      { error_line(" unknown command"); }
void cmd_error_unknown_para(void) { error_line(" unknown para"); }
void cmd_error_cmd_oor(void)      { error_line(" command out of range"); }
void cmd_error_chksum(void)       { error_line(" chksum"); }

struct name { uint16_t w; const char *s; };

static const char *lookup(const struct name *t, uint16_t w)
{
	for (; t->s; t++)
		if (t->w == w)
			return t->s;
	return NULL;
}

static const struct name names_one[] = {
	{0x6269, "biquad"}, {0x636F, "com"}, {0x6465, "delay"}, {0x6576, "event"},
	{0x6578, "extender"}, {0x6761, "gain"}, {0x6932, "i2c"}, {0x6D69, "mixer"},
	{0x6D6F, "modc"}, {0x6D75, "mute"}, {0x7068, "phase"}, {0x7072, "preset"},
	{0x7379, "system"}, {0, NULL}
};

static const struct name names_two[] = {
	{0x002A, ".*"}, {0x6261, ".baud"}, {0x626C, ".block"}, {0x626F, ".boot"},
	{0x636F, ".copy"}, {0x6366, ".cfg"}, {0x6368, ".channel"}, {0x6379, ".cycles"},
	{0x6465, ".debug"}, {0x6563, ".echo"}, {0x656E, ".enable"}, {0x6665, ".feature"},
	{0x6669, ".firmware"}, {0x6775, ".gui"}, {0x6861, ".hardware"}, {0x6972, ".ir"},
	{0x6B65, ".key"}, {0x6C65, ".left"}, {0x6C6F, ".loaded"}, {0x6D6F, ".monitor"},
	{0x6D75, ".mux"}, {0x6F70, ".option"}, {0x6F75, ".out"}, {0x7069, ".ping"},
	{0x7363, ".scale"}, {0x7365, ".select"}, {0x7368, ".shutdown"}, {0x7370, ".spdif"},
	{0x7465, ".temperature"}, {0x7573, ".usb"}, {0x7665, ".version"}, {0, NULL}
};

static const struct name names_three[] = {
	{0x002A, ".*"}, {0x616C, ".all"}, {0x616E, ".analyze"}, {0x6174, ".attributes"},
	{0x6365, ".celsius"}, {0x636D, ".cm"}, {0x636F, ".count"}, {0x6375, ".custom"},
	{0x6461, ".data"}, {0x656E, ".enable"}, {0x6576, ".events"}, {0x6661, ".fahrenheit"},
	{0x6669, ".firmware"}, {0x666C, ".flags"}, {0x6869, ".hide"}, {0x6B65, ".kelvin"},
	{0x6D65, ".memory"}, {0x6D73, ".ms"}, {0x6D6F, ".mode"}, {0x6E61, ".name"},
	{0x6F75, ".out"}, {0x7061, ".panes"}, {0x7365, ".sets"}, {0x7374, ".state"},
	{0x7479, ".type"}, {0x7570, ".update"}, {0x7661, ".value"}, {0x7665, ".version"},
	{0, NULL}
};

static const struct name names_four[] = {
	{0x002A, ".*"}, {0x626F, ".board"}, {0x6275, ".buffer"}, {0x6361, ".calc"},
	{0x636C, ".clear"}, {0x636F, ".com"}, {0x6461, ".data"}, {0x6475, ".dump"},
	{0x6473, ".dsp"}, {0x6563, ".echo"}, {0x6572, ".eram"}, {0x6F66, ".off"},
	{0x6F6E, ".on"}, {0x6F74, ".other"}, {0x7261, ".raw"}, {0x7479, ".type"},
	{0x7570, ".update"}, {0x7665, ".vendor"}, {0, NULL}
};

static void print_input_on_error(uint8_t word)
{
	if (REQ_B(cmd_req_type) == cmd_type_error) {
		out_chr('.');
		out_str(req_input_str(word));
	}
}

void cmd_answer_header(void)
{
	const char *s;
	uint16_t w1 = req_w(cmd_req_word_one), w2 = req_w(cmd_req_word_two);

	switch (REQ_B(cmd_req_type)) {
	case cmd_type_error: out_str(" !"); break;
	case cmd_type_info:  out_str(" *"); break;
	case cmd_type_set:   out_str(" >"); break;
	case cmd_type_get:   out_str(" <"); break;
	case cmd_type_event: out_str(" #"); break;
	}
	out_chr('.');
	out_u16(REQ_B(cmd_req_receiver));
	out_chr(' ');

	/* word one */
	if (w1 != 0x0000) {
		s = lookup(names_one, w1);
		if (s)
			out_str(s);
		else if (REQ_B(cmd_req_type) == cmd_type_error)
			out_str(req_input_str(cmd_req_word_one));
	}

	/* word two */
	if (REQ_B(6) == cmd_req_number) {
		out_chr('.');
		out_u16(REQ_B(5));
	} else if (w2 != 0x0000) {
		if (w2 == 0x6572)
			s = w1 == 0x7379 ? ".errors" : ".erase";
		else if (w2 == 0x696E)
			s = w1 == 0x7379 ? ".info" : ".in";
		else if (w2 == 0x706F)
			s = w1 == 0x6D69 ? ".position" : ".poti";
		else if (w2 == 0x7265)
			s = w1 == 0x7379 ? ".reinit" : ".remove";
		else
			s = lookup(names_two, w2);
		if (s)
			out_str(s);
		else
			print_input_on_error(cmd_req_word_two);
	}

	/* word three */
	uint16_t w3 = req_w(cmd_req_word_three);
	if (REQ_B(8) == cmd_req_number) {
		out_chr('.');
		out_u16(REQ_B(7));
	} else if (w3 != 0x0000) {
		s = NULL;
		if (w3 == 0x6368) {
			s = w2 == 0x6366 ? ".change" : (w2 == 0x6B65 ? ".chk" : ".channel");
		} else if (w3 == 0x696E) {
			/* no else branch in BASCOM: other words print nothing */
			if (w1 == 0x6465) out_str(".inch");
			else if (w1 == 0x6576) out_str(".init");
			else if (w1 == 0x7379) out_str(".info");
			goto word_four;
		} else if (w3 == 0x7363) {
			s = w1 == 0x6761 ? ".scale" : ".scan";
		} else {
			s = lookup(names_three, w3);
		}
		if (s)
			out_str(s);
		else
			print_input_on_error(cmd_req_word_three);
	}

word_four:;
	uint16_t w4 = req_w(cmd_req_word_four);
	if (REQ_B(10) == cmd_req_number) {
		out_chr('.');
		out_u16(REQ_B(9));
	} else if (w4 != 0x0000) {
		s = lookup(names_four, w4);
		if (s) {
			out_str(s);
		} else if (cmd_two_character != 0x0000) {
			out_chr('.');
			out_chr((char)(cmd_two_character >> 8));
			out_chr((char)(cmd_two_character & 0xFF));
			cmd_two_character = 0x0000;
		} else {
			print_input_on_error(cmd_req_word_four);
		}
	}

	switch (REQ_B(cmd_req_type)) {
	case cmd_type_get:
	case cmd_type_set:
	case cmd_type_event: out_chr('='); break;
	case cmd_type_info:
	case cmd_type_error: break;
	default: out_crlf();
	}
}

static void cmd_answer_set(void)
{
	cmd_parse_values_to_vars();
	switch (req_w(cmd_req_word_one)) {
#if BAS_CMD_VERSION_SYSTEM > 0
	case 0x7379: cmd_set_system(); break;
#endif
#ifndef CMD_NO_AUDIO
	case 0x6761: cmd_set_gain(); break;
	case 0x6D75: cmd_set_mute(); break;
	case 0x7068: cmd_set_phase(); break;
	case 0x6465: cmd_set_delay(); break;
	case 0x7072: cmd_set_preset(); break;
#endif
	/* PORT: biquad, com, event, extender, i2c, mixer, modc, select follow
	 * (issue #4); until then "unknown command" */
	default: cmd_error_unknown();
	}
}

static void cmd_answer_get(void)
{
	cmd_parse_values_to_vars();
	switch (req_w(cmd_req_word_one)) {
#if BAS_CMD_VERSION_SYSTEM > 0
	case 0x7379: cmd_get_system(); break;
#endif
#ifndef CMD_NO_AUDIO
	case 0x6761: cmd_get_gain(); break;
	case 0x6D75: cmd_get_mute(); break;
	case 0x7068: cmd_get_phase(); break;
	case 0x6465: cmd_get_delay(); break;
	case 0x7072: cmd_get_preset(); break;
#endif
	default: cmd_error_unknown();
	}
}

void cmd_answer(void)
{
	for (uint8_t i = 1; i <= cmd_reg_var_bytes_split; i++) {
		cmd_reg_var_integers[i] = 0;
		cmd_reg_var_singles[i] = 0.0;
	}
	REQ_B(cmd_req_receiver) = cmd_target_device0;
	uint8_t er = 0;
	for (uint8_t i = 4; i <= 10; i += 2)
		if (REQ_B(i) == cmd_req_number && REQ_B(i - 1) == 0x00)
			er++;
	if (er > 0) {
		cmd_error_cmd_oor();
		return;
	}
	switch (REQ_B(cmd_req_type)) {
	case cmd_type_set: cmd_answer_set(); break;
	case cmd_type_get: cmd_answer_get(); break;
	}
}

void cmd_load_header(const uint16_t words[cmd_req_words_max])
{
	for (uint8_t i = 1; i <= cmd_req_words_max; i++)
		req_w_set(i, words[i - 1]);
}

void cmd_sendevent(void)
{
	uint8_t ch = buart_channel;
	if (!iface_hook_ir_master_online()) {  /* IR master offline: COM master */
		buart_channel = iface_buart_channel[iface_ch_master];
		cmd_answer_header();
		out_str(cmd_req_value);
		out_crlf();
	}
	if (iface_hook_usb_dtr() > 0) {        /* GUI on COM USB */
		buart_channel = iface_buart_channel[iface_ch_usb];
		cmd_answer_header();
		out_str(cmd_req_value);
		out_crlf();
	}
	buart_channel = ch;
}
