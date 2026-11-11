/*
 * Port of libs/cmd/v20/common/subs/cmd_system: baud, boot, channel.name, cycles, debug.*,
 * echo, errors, firmware, info, key.*, ping, cfg.change, feature.attributes, gui.panes,
 * poti, temperature, reinit and hardware.memory.eram (both only with FORGE_FACTORY_RESET).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include <math.h>
#include <string.h>
#include "api.h"
#include "bas_data.h"
#include "events.h"
#include "flavour_data.h"
#include "i2c.h"
#include "store.h"
#include "tas3108.h"
#include "factory.h"
#include "identity.h"
#include "sys_init.h"
void watchdog_kick(void);
#include "cmd.h"
#include "flavour.h"
#include "mcuid.h"
#include "version.h"
#include "eram.h"
#include "registers.h"

/* hooks into later modules */
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

/* PORT extension (#31): set system.key.info="MODEL-PCB" chooses the identity of a build that
 * serves several identical variants (key.info.list); stored in the EEPROM */
static void cmd_set_system_key(void)
{
	if (req_w(cmd_req_word_three) != 0x696E || req_w(cmd_req_word_four) != 0x0000) {
		cmd_error_unknown();
		return;
	}
	if (!identity_set(cmd_req_value)) {  /* quotes already removed by the parser */
		cmd_error_cmd_oor();
		return;
	}
	cmd_answer_ok();
}

/* system.reinit: factory reset at the next start (deletes all tuning). PORT: only with
 * FORGE_FACTORY_RESET, otherwise "unknown command" (the port would only use defaults in RAM). */
static void cmd_set_system_reinit(void)
{
	if (!factory_reset_enabled || req_w(cmd_req_word_three) != 0x0000) {
		cmd_error_unknown();
		return;
	}
	set_register(BAS_REG_STARTUP_STATE, BAS_STARTUP_STATE_SYSINIT);
	cmd_answer_ok();
	mcu_reset_delayed();
}

/* system.hardware.memory.eram: writes 0x0F and 0xF0 to the whole internal EEPROM, reads back,
 * then factory reset. PORT: only with FORGE_FACTORY_RESET. */
static const uint16_t memory_eram_event[cmd_req_words_max] = {0x0104, 0x7379, 0x6861, 0x6D65, 0x6572};

static void hex2s(char *p, uint8_t b)
{
	static const char h[] = "0123456789ABCDEF";
	p[0] = h[b >> 4];
	p[1] = h[b & 15];
}

static void mem_pass(uint8_t v)
{
	uint16_t e = 0;
	for (uint16_t w = 0; w <= BAS_MCU_ERAM_SIZE; w++) {
		eram_wb(w, v);
		watchdog_kick();
	}
	for (uint16_t w = 0; w <= BAS_MCU_ERAM_SIZE; w++) {
		uint8_t r = eram_rb(w);
		if (r != v) {
			e++;
			char t[40] = "error@addr[&H";
			char *p = t + 13;
			hex2s(p, (uint8_t)(w >> 8)); p += 2;  /* hex() of a word: 4 digits */
			hex2s(p, (uint8_t)w); p += 2;
			*p++ = ']'; *p++ = '&'; *p++ = 'H'; hex2s(p, v); p += 2;
			*p++ = ':'; *p++ = '&'; *p++ = 'H'; hex2s(p, r); p += 2;
			*p = 0;
			cmd_load_header(memory_eram_event);
			cmd_answer_header();
			out_str(t);
			out_crlf();
		}
		watchdog_kick();
	}
	if (e == 0) {
		char t[12] = "ok(&H";
		hex2s(t + 5, v);
		t[7] = ')';
		t[8] = 0;
		cmd_load_header(memory_eram_event);
		cmd_answer_header();
		out_str(t);
		out_crlf();
	}
}

static void cmd_set_system_hardware(void)
{
	if (!factory_reset_enabled || req_w(cmd_req_word_three) != 0x6D65 || req_w(cmd_req_word_four) != 0x6572) {
		cmd_error_unknown();
		return;
	}
	cmd_answer_ok();
	mem_pass(0x0F);
	mem_pass(0xF0);
	set_register(BAS_REG_STARTUP_STATE, BAS_STARTUP_STATE_SYSINIT);
	mcu_reset_delayed();
}

/* system.cfg.change=on|off: off keeps the outputs muted after changes until set back
 * (auto unmute counter "on"), on unmutes 8 passes after the last change */
static void cmd_set_system_cfg(void)
{
	if (req_w(cmd_req_word_three) != 0x6368 || req_w(cmd_req_word_four) != 0x0000) {
		cmd_error_unknown();
		return;
	}
	if (!(cmd_reg_var_states[1] & (1 << cmd_reg_state_isBoolean))) {
		cmd_error_unknown_para();
		return;
	}
	api_autounmute_counter = cmd_reg_var_integers[1] == 0 ? BAS_API_AUTOUNMUTE_SET : BAS_API_AUTOUNMUTE_ON;
	cmd_answer_ok();
}

/* PORT extension (#40): system.startup.N="<command>" stores a start command (N 1..8, up to
 * 32 characters, "" deletes), written with ':' instead of '=' (e.g. "set.1 gain.out.2:150"):
 * the parser keeps only the text after the last '='. The commands run once after the first
 * preset load of a start.
 * Commands that restart or reset the device are refused (endless restart). */
static void cmd_set_system_startup(void)
{
	if (REQ_B(8) != cmd_req_number || req_w(cmd_req_word_four) != 0x0000) {
		cmd_error_unknown();
		return;
	}
	if (!(cmd_reg_var_state & (1 << cmd_reg_state_isString))) {
		cmd_error_unknown_para();
		return;
	}
	uint8_t i = REQ_B(7);
	if (i < 1 || i > ERAM_SYS_INIT_SETS_ERAM_COUNT || strlen(cmd_req_value) >= ERAM_SYS_INIT_SETS_ERAM_SIZE
	    || strstr(cmd_req_value, "system.boot") || strstr(cmd_req_value, "system.reinit")
	    || strstr(cmd_req_value, "system.startup")) {
		cmd_error_cmd_oor();
		return;
	}
	eram_write_str(ERAM_EL(SYS_INIT_SETS_ERAM, i), ERAM_SYS_INIT_SETS_ERAM_SIZE, cmd_req_value);
	cmd_answer_ok();
}

static void print_startup(uint8_t i)
{
	char l[ERAM_SYS_INIT_SETS_ERAM_SIZE + 1];
	sys_init_get(i, l);
	REQ_B(8) = cmd_req_number;
	REQ_B(7) = i;
	cmd_answer_header();
	Q(); out_str(l); Q();
	out_crlf();
}

static void cmd_get_system_startup(void)
{
	if (req_w(cmd_req_word_three) == 0x002A) {
		for (uint8_t i = 1; i <= ERAM_SYS_INIT_SETS_ERAM_COUNT; i++)
			print_startup(i);
	} else if (REQ_B(8) == cmd_req_number && req_w(cmd_req_word_four) == 0x0000) {
		uint8_t i = REQ_B(7);
		if (i >= 1 && i <= ERAM_SYS_INIT_SETS_ERAM_COUNT)
			print_startup(i);
		else
			cmd_error_cmd_oor();
	} else {
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
	case 0x6366: cmd_set_system_cfg(); break;
	case 0x6B65: cmd_set_system_key(); break;
	case 0x6861: cmd_set_system_hardware(); break;
	case 0x7265: cmd_set_system_reinit(); break;
	case 0x7374: cmd_set_system_startup(); break;  /* PORT extension (#40) */
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
	out_str("[\"");
	out_str(identity_model());
	out_str("\","
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
		out_str(BAS_VENDOR_ID "-");
		out_str(identity_model());
		out_chr('-');
		out_str(identity_board());
		out_chr('-');
		out_str(mcuid_string);
		out_str("-CCBYNCSA-" FW_BUILD_DATE "-MDD");
		Q();
		out_crlf();
		break;
	case 0x626F:
		cmd_answer_header();
		Q(); out_str(identity_board()); Q();
		out_crlf();
		break;
	case 0x7479:
		cmd_answer_header();
		Q(); out_str(identity_model()); Q();
		out_crlf();
		break;
	case 0x7665:
		cmd_answer_header();
		Q(); out_str(BAS_VENDOR_ID_LARGE); Q();
		out_crlf();
		break;
	case 0x6C69: {  /* PORT extension (#31): key.info.list = identities this build can take */
		char buf[16];
		cmd_answer_header();
		out_chr('[');
		for (uint8_t i = 0; i < identity_count(); i++) {
			if (i)
				out_chr(',');
			Q(); out_str(identity_name(i, buf)); Q();
		}
		out_chr(']');
		out_crlf();
		break;
	}
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

/* lookupstr(i, label) on a table of NUL-terminated strings */
static void out_lookupstr(const uint8_t *t, uint8_t i)
{
	uint16_t p = 0;
	while (i--)
		while (bas_data_byte(&t[p++]) != 0)
			;
	for (uint8_t c; (c = bas_data_byte(&t[p])) != 0; p++)
		out_chr((char)c);
}

static void cmd_get_system_feature(void)
{
	if (req_w(cmd_req_word_three) != 0x6174) {  /* attributes */
		cmd_error_unknown();
		return;
	}
	uint8_t a = BAS_FEATURE_ATTRIBUTES_NULL;
	switch (req_w(cmd_req_word_four)) {
	case 0x0000: cmd_answer_header(); out_str(BAS_FEATURE_ATTRIBUTES); out_crlf(); return;
	case 0x6672: a = BAS_FEATURE_ATTRIBUTES_FR; break;
	case 0x6877: a = BAS_FEATURE_ATTRIBUTES_HW; break;
	case 0x6F61: a = BAS_FEATURE_ATTRIBUTES_OA; break;
	case 0x6F62: a = BAS_FEATURE_ATTRIBUTES_OB; break;
	}
	if (a == BAS_FEATURE_ATTRIBUTES_NULL) {
		cmd_error_unknown();
		return;
	}
	cmd_two_character = req_w(cmd_req_word_four);
	cmd_answer_header();
	if (a == BAS_FEATURE_ATTRIBUTES_HW) {  /* expected and found peripherals */
		out_chr('[');
		out_u16(BAS_CHK_CHANNEL_OPEN); out_chr(','); out_u16(channel_open); out_chr(',');
		out_u16(BAS_CHK_HARDWARE_DSP); out_chr(','); out_u16(hardware_dsp); out_chr(',');
		out_u16(BAS_CHK_HARDWARE_OTHER); out_chr(','); out_u16(hardware_other);
		out_chr(']');
	} else {
		out_lookupstr(data_feature_attributes, a);
	}
	out_crlf();
}

static void cmd_get_system_gui(void)
{
	if (req_w(cmd_req_word_three) != 0x7061) {  /* panes */
		cmd_error_unknown();
		return;
	}
	uint8_t a = BAS_GUI_PANES_NULL;
	switch (req_w(cmd_req_word_four)) {
	case 0x0000: cmd_answer_header(); out_str(BAS_GUI_PANES); out_crlf(); return;
	case 0x6271: a = BAS_GUI_PANES_BQ; break;
	case 0x696E: a = BAS_GUI_PANES_IN; break;
	case 0x6F75: a = BAS_GUI_PANES_OU; break;
	case 0x7379: a = BAS_GUI_PANES_SY; break;
	}
	if (a == BAS_GUI_PANES_NULL) {
		cmd_error_unknown();
		return;
	}
	cmd_two_character = req_w(cmd_req_word_four);
	cmd_answer_header();
	out_lookupstr(data_gui_panes, a);
	out_crlf();
}

static void cmd_get_system_poti(void)
{
	switch (req_w(cmd_req_word_three)) {
	case 0x2301: cmd_answer_header(); out_u16(events_adc_values[1]); out_crlf(); break;
	case 0x2302: cmd_answer_header(); out_u16(events_adc_values[2]); out_crlf(); break;
	default: cmd_error_unknown();
	}
}

/* str(d, 2) */
static void out_fixed2(double d)
{
	int32_t v = bas_round_i32(d * 100.0);
	if (v < 0) {
		out_chr('-');
		v = -v;
	}
	out_u16((uint16_t)(v / 100));
	out_chr('.');
	out_chr((char)('0' + v / 10 % 10));
	out_chr((char)('0' + v % 10));
}

static void cmd_get_system_temp(void)
{
	if (events_adc_values[4] < events_adc_McuMaxTempOOR) {
		cmd_answer_header();
		out_str("nop");
		out_crlf();
		return;
	}
	double d = (double)(int32_t)events_adc_values[4] * events_adc_temp_correction;  /* kelvin */
	switch (req_w(cmd_req_word_three)) {
	case 0x0000:
	case 0x6B65: break;                                    /* kelvin */
	case 0x6365: d -= BAS_EVENTS_ADC_KELVIN2CELCIUS; break; /* celsius */
	case 0x6661: d = (d - BAS_EVENTS_ADC_KELVIN2CELCIUS) * 1.8 + 32; break;
	default: cmd_error_unknown(); return;
	}
	cmd_answer_header();
	out_fixed2(d);
	out_crlf();
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
	case 0x7374: cmd_get_system_startup(); break;  /* PORT extension (#40) */
	case 0x6665: cmd_get_system_feature(); break;
	case 0x6775: cmd_get_system_gui(); break;
	case 0x706F: cmd_get_system_poti(); break;
	case 0x7465: cmd_get_system_temp(); break;
	default: cmd_error_unknown();
	}
}
