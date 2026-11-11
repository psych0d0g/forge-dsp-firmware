/*
 * Text protocol, port of libs/cmd/v20 (BASCOM firmware 1.11.6000).
 * Names and indices follow the BASCOM source (1-based arrays) so that the port can be
 * compared line by line. License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#ifndef CMD_H_
#define CMD_H_

#include <stdint.h>

/* --- constants of libs/cmd/v20/common/declare.bas --- */
#define cmd_short_error 0x21
#define cmd_short_info  0x2A
#define cmd_short_set   0x3E
#define cmd_short_get   0x3C

#define cmd_reg_unset      0x00
#define cmd_req_type       1
#define cmd_req_receiver   2
#define cmd_req_word_one   2
#define cmd_req_word_two   3
#define cmd_req_word_three 4
#define cmd_req_word_four  5
#define cmd_req_bytes_max  10
#define cmd_req_words_max  5
#define cmd_req_number     0x23

#define cmd_req_inputs_max 80
#define cmd_req_input_max  16
#define cmd_req_value_size 127

#define cmd_chksum_state_empty 0
#define cmd_chksum_state_ok    1
#define cmd_chksum_state_err   2

#define cmd_reg_var_bytes_split 8
#define cmd_reg_var_string_len  23

#define cmd_reg_state_isArray   0
#define cmd_reg_state_isString  1
#define cmd_reg_state_isBoolean 2
#define cmd_reg_state_isSingle  3

#define cmd_state_Exit        0
#define cmd_state_Cmd         1
#define cmd_state_Target      2
#define cmd_state_Register    3
#define cmd_state_Value_begin 4
#define cmd_state_Value_end   5
#define cmd_state_ChkSum      6
#define cmd_state_Spacer      7
#define cmd_state_Equal       8
#define cmd_state_Dot         9

#define cmd_type_nothing 0
#define cmd_type_help    1
#define cmd_type_get     2
#define cmd_type_set     3
#define cmd_type_event   4
#define cmd_type_info    16
#define cmd_type_error   17

#define cmd_target_nothing 0
#define cmd_target_device0 1
#define cmd_target_device1 2
#define cmd_target_device7 8
#define cmd_target_master  16
#define cmd_target_slave   17
#define cmd_target_this    18
#define cmd_target_all     255

/* --- request state, memory layout as BASCOM (cmd_req_bytes, then cmd_req_inputs) ---
 * cmd_req_words(n) overlays cmd_req_bytes(1): word n = bytes(2n-1) | bytes(2n) << 8.
 * BASCOM reads words(6..) past the array into cmd_req_inputs; cmd_mem keeps that order. */
extern uint8_t cmd_mem[cmd_req_bytes_max + cmd_req_inputs_max];
#define REQ_B(i)      (cmd_mem[(i) - 1])
#define REQ_INPUT(i)  (cmd_mem[cmd_req_bytes_max + (i) - 1])  /* cmd_req_inputs(i) */
uint16_t req_w(uint8_t n);                                   /* cmd_req_words(n) */
void req_w_set(uint8_t n, uint16_t v);
const char *req_input_str(uint8_t n);                        /* cmd_req_input(n), string*15 */

extern char cmd_req_value[cmd_req_value_size + 2];          /* cmd_req_values(128) */
extern char cmd_req_chksum[5];
extern uint8_t cmd_req_chksum_value;
extern uint8_t cmd_req_chksum_state;
extern uint16_t cmd_two_character;

extern uint8_t cmd_reg_var_count;
extern char cmd_reg_var_strings[cmd_reg_var_bytes_split][cmd_reg_var_string_len + 1];
extern int16_t cmd_reg_var_integers[cmd_reg_var_bytes_split + 1];  /* 1-based */
extern double cmd_reg_var_singles[cmd_reg_var_bytes_split + 1];    /* 1-based */
extern uint8_t cmd_reg_var_state;
extern uint8_t cmd_reg_var_states[cmd_reg_var_bytes_split + 1];    /* 1-based */

/* --- parser / answers (cmd_core.c) --- */
void cmd_default_clear_req(void);
void cmd_default_parse(const uint8_t *buffer, uint8_t size);  /* buffer is 1-based: buffer[1..size] */
void cmd_parse_values_to_vars(void);
void cmd_answer(void);
void cmd_run_line(const char *line);  /* PORT: parse and answer one line (SD, #40) */
void cmd_answer_header(void);
void cmd_answer_ok(void);
void cmd_error_unknown(void);
void cmd_error_unknown_para(void);
void cmd_error_cmd_oor(void);
void cmd_error_chksum(void);

/* --- routing between the four channels (cmd_route.c) --- */
void cmd_init(void);
void cmd_check4buffer(void);

/* --- module dispatch (cmd_<module>.c); missing modules answer "unknown command" --- */
void cmd_set_system(void);
void cmd_get_system(void);
void cmd_set_gain(void);
void cmd_get_gain(void);
void cmd_set_mute(void);
void cmd_get_mute(void);
void cmd_set_phase(void);
void cmd_get_phase(void);
void cmd_set_delay(void);
void cmd_get_delay(void);
void cmd_set_preset(void);
void cmd_get_preset(void);
void cmd_set_mixer(void);
void cmd_get_mixer(void);
void cmd_set_biquad(void);
void cmd_get_biquad(void);
void cmd_set_event(void);
void cmd_get_event(void);
void cmd_set_com(void);
void cmd_set_modc(void);
void cmd_get_modc(void);
void cmd_set_extender(void);
void cmd_get_extender(void);
void cmd_set_i2c(void);
void cmd_get_i2c(void);

/* events to the GUI: header words from a table (BASCOM: restore + read), value in cmd_req_value */
void cmd_load_header(const uint16_t words[cmd_req_words_max]);
void cmd_sendevent(void);

/* --- output on the current channel (BASCOM: print #buart_channel) --- */
extern uint8_t buart_channel;
void out_str(const char *s);
void out_chr(char c);
void out_u16(uint16_t v);       /* print of a numeric variable / str() */
void out_i16(int16_t v);
void out_crlf(void);

/* crc16 of BASCOM (CRC-16/XMODEM) */
uint16_t bas_crc16(const uint8_t *p, uint16_t len);

#endif
