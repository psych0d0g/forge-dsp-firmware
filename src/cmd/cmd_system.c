/*
 * Port of libs/cmd/v20/common/subs/cmd_system: baud, boot, channel.name, cycles, debug.*,
 * echo, errors, firmware, info, key.*, ping.
 * Not ported yet (answer "unknown command"): cfg.change, feature, gui, poti, temperature
 * (need api/events, issue #4), hardware.memory.eram (full EEPROM test + sysinit) and
 * reinit: a factory reset is only safe once every module with sysinit defaults is ported.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include <string.h>
#include "cmd.h"
#include "flavour.h"
#include "mcuid.h"
#include "version.h"
#include "eram.h"
#include "registers.h"

/* hooks into later modules */
void api_set_out_mute_change(void) __attribute__((weak));
void api_set_out_mute_change(void) {}
void mcu_reset_delayed(void);  /* power.c: debug_close, waitms 1000, software reset */

#define Q() out_chr('"')

/* --- set ----------------------------------------------------------------------- */

static void cmd_set_system_boot(void)
{
	if (req_w(cmd_req_word_three) != 0x0000) {
		cmd_error_unknown();
	} else {
		cmd_answer_ok();
		mcu_reset_delayed();
	}
}

static void cmd_set_system_baud(void)
{
	uint8_t v = (uint8_t)cmd_reg_var_integers[1];
	if (REQ_B(8) != cmd_req_number) {
		cmd_error_unknown();
		return;
	}
	/* only #2..#4 (mediaport, RS485 master, RS485 slave); the USB-COM rate stays */
	if (REQ_B(7) >= 0x02 && REQ_B(7) <= 0x04) {
		set_register((uint8_t)(REQ_B(7) - 1 + BAS_REG_BAUD_CH1), v);
		cmd_answer_ok();
	} else {
		cmd_error_unknown_para();
	}
}

static void cmd_set_system_channel_name(void)
{
	if (REQ_B(10) != cmd_req_number) {
		cmd_error_unknown();
		return;
	}
	if (!(cmd_reg_var_state & (1 << cmd_reg_state_isString))) {
		cmd_error_unknown_para();
		return;
	}
	uint8_t a = REQ_B(9);
	if (a >= 1 && a <= BAS_FLOW_MAX_OUT_CHANNEL) {
		cmd_req_value[BAS_CHANNEL_NAMES_ZERO - 1] = 0;  /* cmd_req_values(channel_names_zero) = 0 */
		eram_write_str(ERAM_EL(CHANNEL_NAMES, a), ERAM_CHANNEL_NAMES_SIZE, cmd_req_value);
		cmd_answer_ok();
	} else {
		cmd_error_cmd_oor();
	}
}

static void cmd_set_system_channel(void)
{
	switch (req_w(cmd_req_word_three)) {
	case 0x6E61: cmd_set_system_channel_name(); break;
	default: cmd_error_unknown();
	}
}

/* select case lo to hi, without type-limit warnings when lo is 0 */
static int in_range(int v, int lo, int hi) { return v >= lo && v <= hi; }

static void cmd_set_system_debug(void)
{
	uint8_t b = (uint8_t)cmd_reg_var_integers[1];
	switch (req_w(cmd_req_word_three)) {
	case 0x6869: {  /* hide: value = crc16(mcuid) - level */
		uint16_t w = bas_crc16((const uint8_t *)mcuid_string, (uint8_t)strlen(mcuid_string));
		w = (uint16_t)(w - (uint16_t)cmd_reg_var_integers[1]);
		if (w <= 6) {
			set_register(BAS_REG_DEBUGHIDE, (uint8_t)w);
			cmd_answer_ok();
		} else {
			set_register(BAS_REG_DEBUGHIDE, BAS_DBGLVL_INIT_HIDE);
			cmd_error_unknown();
		}
		break;
	}
	case 0x6D6F:
		if (in_range(b, BAS_DBGLVL_MIN, BAS_DBGLVL_MAX)) {
			set_register(BAS_REG_DEBUGMODE, b);
			cmd_answer_ok();
		} else {
			cmd_error_cmd_oor();
		}
		break;
	case 0x6F75:
		if (b <= BAS_DEBUGOUT_MAX) {
			set_register(BAS_REG_DEBUGOUT, b);
			cmd_answer_ok();
		} else {
			cmd_error_cmd_oor();
		}
		break;
	default:
		cmd_error_unknown();
	}
}

void cmd_set_system(void)
{
	api_set_out_mute_change();
	switch (req_w(cmd_req_word_two)) {
	case 0x6261: cmd_set_system_baud(); break;
	case 0x626F: cmd_set_system_boot(); break;
	case 0x6368: cmd_set_system_channel(); break;
	case 0x6465: cmd_set_system_debug(); break;
	/* PORT: 0x6366 cfg (api), 0x6861 hardware and 0x7265 reinit (factory reset) later */
	default: cmd_error_unknown();
	}
}

/* --- get ----------------------------------------------------------------------- */

static void print_channel_name(uint8_t a)
{
	char n[ERAM_CHANNEL_NAMES_SIZE];
	eram_read_str(ERAM_EL(CHANNEL_NAMES, a), sizeof n, n);
	cmd_answer_header();
	Q(); out_str(n); Q();
	out_crlf();
}

static void cmd_get_system_channel(void)
{
	if (req_w(cmd_req_word_three) != 0x6E61) {
		cmd_error_unknown();
		return;
	}
	if (req_w(cmd_req_word_four) == 0x002A) {
		REQ_B(10) = cmd_req_number;
		for (uint8_t a = 1; a <= BAS_FLOW_MAX_OUT_CHANNEL; a++) {
			REQ_B(9) = a;
			print_channel_name(a);
		}
	} else if (REQ_B(10) == cmd_req_number) {
		uint8_t a = REQ_B(9);
		if (a >= 1 && a <= BAS_FLOW_MAX_OUT_CHANNEL)
			print_channel_name(a);
		else
			cmd_error_cmd_oor();
	} else {
		cmd_error_unknown();
	}
}

static void cmd_get_system_cycles(void)
{
	cmd_answer_header();
	out_u16(eram_rw(ERAM_DEVICE_BOOTCYCLES_ERAM));
	out_crlf();
}

static void cmd_get_system_debug(void)
{
	switch (req_w(cmd_req_word_three)) {
	case 0x6D6F: cmd_answer_header(); out_u16(registers[BAS_REG_DEBUGMODE]); out_crlf(); break;
	case 0x6F75: cmd_answer_header(); out_u16(registers[BAS_REG_DEBUGOUT]); out_crlf(); break;
	default: cmd_error_unknown();
	}
}

static void cmd_get_system_errors(void)
{
	if (req_w(cmd_req_word_three) != 0x0000) {
		cmd_error_unknown();
		return;
	}
	cmd_answer_header();
	out_chr('[');
	uint8_t c = 0;
	for (uint8_t i = 1; i <= BAS_ERROR_COUNT_MAX; i++) {
		if (error_count_type[i] < 0xFF) {
			if (c > 0)
				out_chr(',');
			out_chr('[');
			out_u16(error_count_type[i]);
			out_chr(',');
			out_u16(eram_rw(ERAM_EL(ERROR_COUNT_ERAM, i)));
			out_chr(']');
			c++;
		}
	}
	out_chr(']');
	out_crlf();
}

static void cmd_get_system_echo(void)
{
	cmd_answer_header();
	Q(); out_str(cmd_req_value); Q();
	out_crlf();
}

static void cmd_get_system_firmware(void)
{
	/* the BASCOM select repeats word two, kept */
	if (req_w(cmd_req_word_two) != 0x6669) {
		cmd_error_unknown();
		return;
	}
	switch (req_w(cmd_req_word_three)) {
	case 0x0000:
		/* version(2): protocol level of the BASCOM firmware, the GUI decides features by it */
		cmd_answer_header();
		Q(); out_str(FW_COMPAT_VERSION); Q();
		out_crlf();
		break;
	case 0x706F:
		/* NEW (not in BASCOM, which answers "unknown command"): system.firmware.port;
		 * ".port" is not in the header tables, so the header is written here */
		out_str(" <.");
		out_u16(REQ_B(cmd_req_receiver));
		out_str(" system.firmware.port=");
		Q(); out_str(FW_VERSION_STR); Q();
		out_crlf();
		break;
	default:
		cmd_error_unknown();
	}
}

#define XSTR(x) STR(x)
#define STR(x) #x

static void cmd_get_system_info(void)
{
	if (req_w(cmd_req_word_three) != 0x0000) {
		cmd_error_unknown();
		return;
	}
	cmd_answer_header();
	out_str("[\"" FLAVOUR_MODEL "\","
	        XSTR(BAS_FLOW_SAMPLE_RATE) "," XSTR(BAS_FLOW_MAX_AD_PAIR) ","
	        XSTR(BAS_FLOW_MAX_SPDIF_PAIR) "," XSTR(BAS_FLOW_MAX_OUT_PAIRS) ","
	        XSTR(BAS_MODUL_MAX_PRESET) ",32]");
	out_crlf();
}

static void cmd_get_system_key_info(void)
{
	switch (req_w(cmd_req_word_four)) {
	case 0x0000:
		cmd_answer_header();
		Q();
		out_str(BAS_VENDOR_ID "-" FLAVOUR_MODEL "-" FLAVOUR_BOARD "-");
		out_str(mcuid_string);
		out_str("-CCBYNCSA-" FW_BUILD_DATE "-MDD");
		Q();
		out_crlf();
		break;
	case 0x626F:
		cmd_answer_header();
		Q(); out_str(FLAVOUR_BOARD); Q();
		out_crlf();
		break;
	case 0x7479:
		cmd_answer_header();
		Q(); out_str(FLAVOUR_MODEL); Q();
		out_crlf();
		break;
	case 0x7665:
		cmd_answer_header();
		Q(); out_str(BAS_VENDOR_ID_LARGE); Q();
		out_crlf();
		break;
	default:
		cmd_error_unknown();
	}
}

static void cmd_get_system_key(void)
{
	switch (req_w(cmd_req_word_three)) {
	case 0x696E: cmd_get_system_key_info(); break;
	default: cmd_error_unknown();
	}
}

static void cmd_get_system_ping(void)
{
	if (req_w(cmd_req_word_three) == 0x0000) {
		cmd_answer_header();
		out_str("\"pong\"");
		out_crlf();
	} else {
		cmd_error_unknown();
	}
}

void cmd_get_system(void)
{
	switch (req_w(cmd_req_word_two)) {
	case 0x6368: cmd_get_system_channel(); break;
	case 0x6379: cmd_get_system_cycles(); break;
	case 0x6465: cmd_get_system_debug(); break;
	case 0x6563: cmd_get_system_echo(); break;
	case 0x6572: cmd_get_system_errors(); break;
	case 0x6669: cmd_get_system_firmware(); break;
	case 0x696E: cmd_get_system_info(); break;
	case 0x6B65: cmd_get_system_key(); break;
	case 0x7069: cmd_get_system_ping(); break;
	/* PORT: 0x6665 feature, 0x6775 gui, 0x706F poti, 0x7465 temperature follow (issue #4) */
	default: cmd_error_unknown();
	}
}
