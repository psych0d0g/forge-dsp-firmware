/*
 * Port of libs/cmd/v20/common/subs/cmd_extender (version 20).
 *   set/get extender.ir.analyze.echo, extender.ir.custom.N=[header,address,command] (1..18),
 *   get extender.ir.custom.*, extender.ir.state
 * extender.usb.firmware.clear/update are empty in the original (no answer), kept so.
 * PORT: custom slot 0 is refused (the original writes before the arrays).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "cmd.h"
#include "ir.h"

#define N REQ_B(9)
#define N_IS_NUM (REQ_B(10) == cmd_req_number)

void cmd_set_extender(void)
{
	switch (req_w(cmd_req_word_two)) {
	case 0x6972:  /* ir */
		switch (req_w(cmd_req_word_three)) {
		case 0x616E:  /* analyze */
			if (req_w(cmd_req_word_four) == 0x6563) {
				irreceiver_analyze_echo = cmd_reg_var_integers[1] > 0;
				cmd_answer_ok();
			} else {
				cmd_error_unknown();
			}
			break;
		case 0x6375:  /* custom */
			if (!N_IS_NUM)
				cmd_error_unknown();
			else if (N < 1 || N > IR_CUSTOM_MAX)
				cmd_error_cmd_oor();
			else if (!(cmd_reg_var_state & (1 << cmd_reg_state_isArray)))
				cmd_error_unknown();
			else {
				ir_custom_set(N, (uint16_t)cmd_reg_var_integers[1], (uint16_t)cmd_reg_var_integers[2],
				              (uint16_t)cmd_reg_var_integers[3]);
				cmd_answer_ok();
			}
			break;
		default: cmd_error_unknown();
		}
		break;
	case 0x7573:  /* usb */
		if (req_w(cmd_req_word_three) == 0x6669 &&
		    (req_w(cmd_req_word_four) == 0x636C || req_w(cmd_req_word_four) == 0x7570))
			break;  /* firmware.clear / .update: empty subs */
		cmd_error_unknown();
		break;
	default: cmd_error_unknown();
	}
}

static void custom_line(uint8_t i)
{
	cmd_answer_header();
	out_chr('[');
	out_u16(irreceiver_custom[1][i]);
	out_chr(',');
	out_u16(irreceiver_custom[2][i]);
	out_chr(',');
	out_u16(irreceiver_custom[3][i]);
	out_chr(']');
	out_crlf();
}

void cmd_get_extender(void)
{
	switch (req_w(cmd_req_word_two)) {
	case 0x6972:
		switch (req_w(cmd_req_word_three)) {
		case 0x616E:
			if (req_w(cmd_req_word_four) == 0x6563) {
				cmd_answer_header();
				out_u16(irreceiver_analyze_echo);
				out_crlf();
			} else {
				cmd_error_unknown();
			}
			break;
		case 0x6375:
			if (N_IS_NUM) {
				if (N < 1 || N > IR_CUSTOM_MAX)
					cmd_error_cmd_oor();
				else
					custom_line(N);
			} else if (req_w(cmd_req_word_four) == 0x002A) {
				REQ_B(10) = cmd_req_number;
				for (uint8_t i = 1; i <= IR_CUSTOM_MAX; i++) {
					REQ_B(9) = i;
					custom_line(i);
				}
			} else {
				cmd_error_unknown();
			}
			break;
		case 0x7374:  /* state */
			if (req_w(cmd_req_word_four) == 0x0000) {
				cmd_answer_header();
				out_u16(irreceiver_state);
				out_crlf();
			} else {
				cmd_error_unknown();
			}
			break;
		default: cmd_error_unknown();
		}
		break;
	case 0x7573:
		if (req_w(cmd_req_word_three) != 0x7573)
			cmd_error_unknown();
		break;  /* extender.usb.usb: empty in the original */
	default: cmd_error_unknown();
	}
}
