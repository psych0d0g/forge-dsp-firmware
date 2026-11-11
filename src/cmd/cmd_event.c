/*
 * Port of libs/cmd/v20/common/subs/cmd_event and cmd_com (version 20).
 *   set event.N.init=v / event.N.value=v, set event.block.N=[a,b,c,d] (events 77+4N..)
 *   get event.N.init / event.N.value, get event.monitor=[n1..n5] (answered as event)
 *   set com.N=text: text out on BASCOM channel #N (1 USB, 3 RS485 master, 4 RS485 slave)
 * PORT deviations:
 * - get event.N.*: the original checks bytes 3/4 (the "event" word) for the number and so
 *   never answers single events; the port reads the number from word two like set
 * - event numbers 0, beyond max_events and blocks beyond event 128 are refused; monitor
 *   prints 0 for numbers beyond max_events (the original reads beyond the array)
 * event.block answers nothing, as in the original.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "cmd.h"
#include "events.h"
#include "flavour.h"

void cmd_set_event(void)
{
	if (REQ_B(6) == cmd_req_number) {
		uint8_t nr = REQ_B(5);
		if (nr < 1 || nr > BAS_MAX_EVENTS) {
			cmd_error_cmd_oor();
			return;
		}
		switch (req_w(cmd_req_word_three)) {
		case 0x696E:  /* init */
			events_init_set(nr, (uint8_t)cmd_reg_var_integers[1]);
			cmd_answer_ok();
			break;
		case 0x7661:  /* value */
			event_set_integer(nr, cmd_reg_var_integers[1]);
			cmd_answer_ok();
			break;
		default: cmd_error_unknown();
		}
	} else if (req_w(cmd_req_word_two) == 0x626C) {  /* block */
		if (REQ_B(8) != cmd_req_number) {
			cmd_error_unknown();
			return;
		}
		uint16_t nr = (uint16_t)(REQ_B(7) * 4 + 0x4D);
		if (nr + 3 > BAS_MAX_EVENTS) {
			cmd_error_cmd_oor();
			return;
		}
		for (uint8_t i = 1; i <= 4; i++)
			event_set_integer((uint8_t)nr++, cmd_reg_var_integers[i]);
	} else {
		cmd_error_unknown();
	}
}

void cmd_get_event(void)
{
	if (REQ_B(6) == cmd_req_number) {
		uint8_t nr = REQ_B(5);
		if (nr < 1 || nr > BAS_MAX_EVENTS) {
			cmd_error_cmd_oor();
			return;
		}
		switch (req_w(cmd_req_word_three)) {
		case 0x696E: cmd_answer_header(); out_u16(events_init_get(nr)); out_crlf(); break;
		case 0x7661: cmd_answer_header(); out_u16(events_field_value[nr]); out_crlf(); break;
		default: cmd_error_unknown();
		}
		return;
	}
	if (req_w(cmd_req_word_two) != 0x6D6F) {  /* monitor */
		cmd_error_unknown();
		return;
	}
	if (!(cmd_reg_var_state & (1 << cmd_reg_state_isArray))) {
		cmd_error_unknown_para();
		return;
	}
	REQ_B(cmd_req_type) = cmd_type_event;
	cmd_answer_header();
	out_chr('[');
	for (uint8_t i = 1; i <= 5; i++) {
		uint8_t nr = (uint8_t)cmd_reg_var_integers[i];
		out_u16(nr > 0 && nr <= BAS_MAX_EVENTS ? events_field_value[nr] : 0);
		if (i < 5)
			out_chr(',');
	}
	out_chr(']');
	out_crlf();
}

/* BASCOM file channels: #1 COM1 (buart 0), #2 COM3 (buart 2), #3 COM2 (buart 1), #4 COM4 (buart 3) */
static void print_on(uint8_t buart, const char *end)
{
	uint8_t ch = buart_channel;
	buart_channel = buart;
	out_str(cmd_req_value);
	out_str(end);
	buart_channel = ch;
}

void cmd_set_com(void)
{
	if (REQ_B(6) != cmd_req_number) {
		cmd_error_unknown();
		return;
	}
	switch (REQ_B(5)) {
	case 1: print_on(0, "\r\n"); break;
#if BAS_EXT_BT720 > 0
	case 2: print_on(2, "\r"); break;
#endif
	case 3: print_on(1, "\r\n"); break;
	case 4: print_on(3, "\r\n"); break;
	default: cmd_error_unknown();
	}
}
