/*
 * Port of libs/cmd/v20/common/subs/cmd_preset (version 20): preset.copy, .erase, .loaded,
 * .select (set) and .loaded, .monitor, .select (get).
 * PORT: preset numbers 0..modul_max_preset-1 only (original: up to modul_max_preset).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "cmd.h"
#include "api.h"
#include "modc.h"
#include "flavour.h"
#include "registers.h"
#include "store.h"

void cmd_set_preset(void)
{
	int16_t v = cmd_reg_var_integers[1];
	if (req_w(cmd_req_word_three) != 0x0000) {
		cmd_error_unknown();
		return;
	}
	/* PORT: the original accepts v = modul_max_preset (off by one): with 4 presets that is an
	 * alias of preset 0 (erase 4 = erase 0), with 8 presets it reads beyond the address tables */
	if (v < 0 || v >= BAS_MODUL_MAX_PRESET) {
		cmd_error_cmd_oor();
		return;
	}
	uint8_t preset;
	switch (req_w(cmd_req_word_two)) {
	case 0x636F:  /* copy */
		if (store_preset >= BAS_MODUL_MAX_PRESET) {  /* PORT: no preset loaded yet */
			cmd_error_cmd_oor();
			break;
		}
		cmd_answer_ok();
		store_preset_copy_from = store_preset;
		store_preset_copy_to = (uint8_t)v;
		store_copy_preset();
		break;
	case 0x6572:  /* erase */
		cmd_answer_ok();
		if (store_preset == (uint8_t)v)
			modc_chk_affect();
		preset = store_preset;
		store_preset = (uint8_t)v;
		store_erase_preset();
		store_preset = preset;
		break;
	case 0x6C6F:  /* loaded */
		cmd_answer_ok();
		modc_request_preset((uint8_t)v);
		break;
	case 0x7365:  /* select: preset after the next start */
		cmd_answer_ok();
		preset = store_preset;
		store_preset = (uint8_t)v;
		store_set_preset();
		store_preset = preset;
		break;
	default: cmd_error_unknown();
	}
}

void cmd_get_preset(void)
{
	switch (req_w(cmd_req_word_two)) {
	case 0x6C6F:  /* loaded */
		cmd_answer_header();
		out_u16(store_preset);
		out_crlf();
		break;
	case 0x6D6F:  /* monitor */
	case 0x7365:  /* select */
		cmd_answer_header();
		out_u16(registers[BAS_REG_LOAD_PRESET]);
		out_crlf();
		break;
	default: cmd_error_unknown();
	}
}
