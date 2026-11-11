/*
 * Port of libs/cmd/v20/common/subs/cmd_system (part 1): boot, echo, firmware, info,
 * key.*, ping. Not ported yet (answer "unknown command"): baud, cfg, channel, cycles,
 * debug, errors, feature, gui, hardware, poti, reinit, temperature (issue #1).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "cmd.h"
#include "flavour.h"
#include "mcuid.h"
#include "version.h"

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

void cmd_set_system(void)
{
	api_set_out_mute_change();
	switch (req_w(cmd_req_word_two)) {
	case 0x626F: cmd_set_system_boot(); break;
	/* PORT: 0x6261 baud, 0x6366 cfg, 0x6368 channel, 0x6465 debug, 0x6861 hardware,
	 * 0x7265 reinit follow with M2 (registers/store) */
	default: cmd_error_unknown();
	}
}

/* --- get ----------------------------------------------------------------------- */

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
	case 0x6563: cmd_get_system_echo(); break;
	case 0x6669: cmd_get_system_firmware(); break;
	case 0x696E: cmd_get_system_info(); break;
	case 0x6B65: cmd_get_system_key(); break;
	case 0x7069: cmd_get_system_ping(); break;
	/* PORT: 0x6368 channel, 0x6379 cycles, 0x6465 debug, 0x6572 errors, 0x6665 feature,
	 * 0x6775 gui, 0x706F poti, 0x7465 temperature follow (issue #1) */
	default: cmd_error_unknown();
	}
}
