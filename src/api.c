/*
 * Port of libs/api (functions.bas, mute/functions.bas, mix/functions.bas) and the load
 * sequence of modc_load_preset. License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "api.h"
#include "sys_init.h"
#include "bas_data.h"
#include "biquad.h"
#include "board.h"
#include "cmd.h"
#include "ct7302.h"
#include "dac.h"
#include "dsp.h"
#include "eram.h"
#include "events.h"
#include "modc.h"
#include "flavour.h"
#include "flavour_data.h"
#include "i2c.h"
#include "registers.h"
#include "store.h"
#include "tas3108.h"

#ifndef FORGE_AUDIO
#define FORGE_AUDIO 0
#endif

uint8_t api_store = 1;  /* api_init */
int16_t api_autounmute_counter;
static uint8_t api_mute_out_prio[5];  /* 1-based */
uint32_t store_mix_changed;

#define SV(base, i) storage_non_biquadvalues[(base) + (i) - 1]
#define storage_mute_out storage_non_biquadvalues[BAS_STORAGE_MUTE_OUT_BASE]
#define storage_phase    storage_non_biquadvalues[BAS_STORAGE_PHASE_BASE]
#define store_mix_position storage_non_biquadvalues[BAS_STORE_MIX_POSITION_BASE]

static uint16_t sw_get(uint16_t base, uint8_t i)
{
	uint16_t a = (uint16_t)(base + (i - 1) * 2);
	return storage_non_biquadvalues[a] | (uint16_t)storage_non_biquadvalues[a + 1] << 8;
}

static void sw_set(uint16_t base, uint8_t i, uint16_t v)
{
	uint16_t a = (uint16_t)(base + (i - 1) * 2);
	storage_non_biquadvalues[a] = v & 0xFF;
	storage_non_biquadvalues[a + 1] = v >> 8;
}

void api_init(void)
{
	if (registers[BAS_REG_STARTUP_STATE] == BAS_STARTUP_STATE_SYSINIT)
		reset_register(BAS_REG_LOAD_PRESET);
	api_store = 1;
}

void api_init_mute(void)
{
	for (uint8_t i = 1; i <= api_mute_out_prio_hardware; i++)
		api_mute_out_prio[i] = api_mute_out_off;
	api_autounmute_counter = BAS_API_AUTOUNMUTE_OFF;
}

void api_set_out_mute(void)
{
	uint8_t mute_value = storage_mute_out;
	for (uint8_t i = 1; i <= api_mute_out_prio_hardware; i++)
		mute_value |= api_mute_out_prio[i];
	dac_set_mute(mute_value);
}

void api_set_out_mute_store(uint8_t v)
{
	storage_mute_out = v;
	api_set_out_mute();
	storage_flags++;
}

uint8_t api_get_out_mute_store(void) { return storage_mute_out; }

uint8_t api_get_out_mute_prio(uint8_t prio)
{
	return prio >= 1 && prio <= api_mute_out_prio_hardware ? api_mute_out_prio[prio] : 0;
}

void api_set_out_mute_prio(uint8_t prio, uint8_t v)
{
	if (api_mute_out_prio[prio] != v) {
		api_mute_out_prio[prio] = v;
		api_set_out_mute();
	}
	if (prio == api_mute_out_prio_change && api_autounmute_counter < BAS_API_AUTOUNMUTE_SET)
		api_autounmute_counter = BAS_API_AUTOUNMUTE_SET;
}

void api_set_out_mute_change(void)
{
	api_set_out_mute_prio(api_mute_out_prio_change, api_mute_out_on);
	if (api_autounmute_counter < BAS_API_AUTOUNMUTE_SET)
		api_autounmute_counter = BAS_API_AUTOUNMUTE_SET;
}

void api_mainloop(void)
{
	if (api_autounmute_counter == BAS_API_AUTOUNMUTE_CALL)
		api_set_out_mute_prio(api_mute_out_prio_change, api_mute_out_off);
	if (api_autounmute_counter > BAS_API_AUTOUNMUTE_MIN)
		api_autounmute_counter--;
	if (api_autounmute_counter == BAS_API_AUTOUNMUTE_CALL)  /* after the last decr, before unmute */
		api_set_mix_reg();
}

/* input volume: DSP core 1, register from data_flow_dsp_input_vol_reg */
void api_set_in_vol(uint8_t ch, uint8_t v)
{
	if (ch >= 1 && ch <= 6) {
		dsp_core = 1;  /* dsp_set_vol -> dsp_1_set_vol */
		dsp_reg = BAS_LOOKUP_B(data_flow_dsp_input_vol_reg, ch);
		dsp_set_vol_out((int32_t)bas_data_word(&data_dsp_vol[v * 4]) |
		                (int32_t)bas_data_word(&data_dsp_vol[v * 4 + 2]) << 16);
	}
	if (api_store == 1) {
		SV(BAS_STORAGE_VOL_IN_BASE, ch) = v;
		storage_flags++;
	}
}

uint8_t api_get_in_vol(uint8_t ch) { return SV(BAS_STORAGE_VOL_IN_BASE, ch); }

void api_set_spdif_vol(uint8_t ch, uint8_t v)
{
	modc_loudness_touch();  /* M6: loudness follows gain.spdif when it has no event source */
#if BAS_MODUL_CT7302 > 0
	ct7302_20_set_vol(ch, v);
#elif BAS_MODUL_SRC43X2 > 0
	src43x2_set_vol(ch, v);
#endif
	if (api_store == 1) {
		SV(BAS_STORAGE_VOL_SPDIF_BASE, ch) = v;
		storage_flags++;
	}
}

uint8_t api_get_spdif_vol(uint8_t ch) { return SV(BAS_STORAGE_VOL_SPDIF_BASE, ch); }

void api_set_out_vol_hw(uint8_t ch, uint8_t v)
{
	if (ch < 1 || ch > 8)
		return;
	dsp_core = BAS_LOOKUP_B(data_flow_dsp_output_vol_dsp, ch);
	dsp_reg = BAS_LOOKUP_B(data_flow_dsp_output_vol_reg, ch);
	dsp_set_vol_out((int32_t)bas_data_word(&data_dsp_vol[v * 4]) |
	                (int32_t)bas_data_word(&data_dsp_vol[v * 4 + 2]) << 16);  /* dsp_bq_vol_calc */
}

void api_set_out_vol(uint8_t ch, uint8_t v)
{
	if (!(modc_active_affect & (1u << BAS_ONOUTVOL)))
		api_set_out_vol_hw(ch, v);
	else
		modc_set_run(BAS_ONOUTVOL);  /* a volumen block computes the outputs */
	if (api_store == 1) {
		SV(BAS_STORAGE_VOL_OUT_BASE, ch) = v;
		storage_flags++;
	}
}

uint8_t api_get_out_vol(uint8_t ch) { return SV(BAS_STORAGE_VOL_OUT_BASE, ch); }

void api_set_phase(uint8_t v)
{
#if BAS_MODUL_TAS3108_OUTPUT_PHASE > 0
	/* dsp_set_phase: per output a DSP register, bit = inverted */
	for (uint8_t i = 1; i <= BAS_FLOW_MAX_OUT_CHANNEL; i++) {
		dsp_core = BAS_LOOKUP_B(data_flow_dsp_output_inv_dsp, i);
		dsp_reg = BAS_LOOKUP_B(data_flow_dsp_output_inv_reg, i);
		dsp_set_phase_reg((v >> (i - 1)) & 1 ? BAS_FLOW_PHASE_ON_VALUE : BAS_FLOW_PHASE_OFF_VALUE);
	}
#else
	dac_set_phase(v);
#endif
	if (api_store == 1) {
		storage_phase = v;
		storage_flags++;
	}
}

uint8_t api_get_phase(void) { return storage_phase; }

/* delays: two channels per DSP register (odd channel = high word) */
static void set_delay_pair(uint16_t base, uint8_t ch, uint16_t v, const uint8_t *tab_dsp, const uint8_t *tab_reg)
{
	uint16_t w1, w2;
	if (ch % 2 == 1) {
		w1 = sw_get(base, (uint8_t)(ch + 1));
		w2 = v;
	} else {
		w1 = v;
		w2 = sw_get(base, (uint8_t)(ch - 1));
	}
	dsp_core = BAS_LOOKUP_B(tab_dsp, ch);
	dsp_reg = BAS_LOOKUP_B(tab_reg, ch);
	dsp_set_delay((uint32_t)w1 | (uint32_t)w2 << 16);  /* buffer_words(1), buffer_words(2) */
}

void api_set_delay(uint8_t ch, uint16_t v)
{
	set_delay_pair(BAS_STORAGE_DELAY_BASE, ch, v, data_flow_pair_delay_dsp, data_flow_pair_delay_reg);
	if (api_store == 1) {
		sw_set(BAS_STORAGE_DELAY_BASE, ch, v);
		storage_flags++;
	}
}

uint16_t api_get_delay(uint8_t ch) { return sw_get(BAS_STORAGE_DELAY_BASE, ch); }

#if BAS_FEATURE_INPUT_DELAY > 0
void api_set_delay_in(uint8_t ch, uint16_t v)
{
	set_delay_pair(BAS_STORAGE_DELAY_IN_BASE, ch, v, data_flow_pair_delay_in_dsp, data_flow_pair_delay_in_reg);
	if (api_store == 1) {
		sw_set(BAS_STORAGE_DELAY_IN_BASE, ch, v);
		storage_flags++;
	}
}

uint16_t api_get_delay_in(uint8_t ch) { return sw_get(BAS_STORAGE_DELAY_IN_BASE, ch); }
#else
void api_set_delay_in(uint8_t ch, uint16_t v) { (void)ch; (void)v; }
uint16_t api_get_delay_in(uint8_t ch) { (void)ch; return 0; }
#endif

uint8_t api_get_mix_position(void) { return store_mix_position; }

void api_set_mix_position(uint8_t pos)
{
	if (store_mix_position != pos) {
		store_mix_position = pos;
		store_mix_changed = BAS_FLOW_MIX_MAX_CHANGED;
		api_set_out_mute_change();
		static const uint16_t ev[cmd_req_words_max] = {0x0104, 0x6D69, 0x706F, 0x0000, 0x0000};
		cmd_req_value[0] = (char)('0' + pos % 10);  /* positions 1..4 */
		cmd_req_value[1] = 0;
		cmd_load_header(ev);
		cmd_sendevent();
	}
}

/* --- mono per output pair (PORT extension #14, MODC type 0x6A) ---------------------------------
 * The input mixer (mixer.P.C.G) has one row per DSP channel: C 1/2 = 35/36, C 3..10 = outputs
 * 1..8 (odd = left), G = source (5 pairs, even = left, odd = right; GUI page INMI21). Mono for
 * output pair p makes the rows of channels 2p-1 and 2p equal when the coefficients are written:
 * 1 stereo, 2 right on both, 3 left on both, 4 sum (L+R)/2. The stored tuning is not changed. */
static uint8_t mix_mono[BAS_FLOW_MAX_OUT_PAIRS + 1];

static uint8_t mix_b(uint8_t c, uint8_t g)  /* storage index of mixer.P.c.g, 0 = none, 0xFF = mux */
{
	return BAS_LOOKUP_B(data_flow_mix_cmd2array, (c - 1) * BAS_FLOW_MIX_MAX_CHANNELS + g);
}

void api_set_mix_mono(uint8_t pair, uint8_t mode)
{
	if (pair < 1 || pair > BAS_FLOW_MAX_OUT_PAIRS)
		return;
	if (mode < 1 || mode > 4)
		mode = 1;
	if (mix_mono[pair] == mode || (mix_mono[pair] <= 1 && mode == 1))
		return;
	mix_mono[pair] = mode;
	api_set_out_mute_change();  /* the mixer is written with the auto unmute, as a mixer change */
	store_mix_changed = BAS_FLOW_MIX_MAX_CHANGED;
}

uint8_t api_get_mix_mono(uint8_t pair)
{
	return pair >= 1 && pair <= BAS_FLOW_MAX_OUT_PAIRS && mix_mono[pair] > 1 ? mix_mono[pair] : 1;
}

/* value written for storage index b of the active position (with the mono overlay) */
static uint8_t mix_value(uint16_t frame, uint8_t b)
{
	for (uint8_t c = 3; c <= BAS_FLOW_MIX_MAX_CHANNELS; c++) {
		uint8_t p = (uint8_t)((c - 1) / 2), mode = p <= BAS_FLOW_MAX_OUT_PAIRS ? mix_mono[p] : 0;
		if (mode <= 1)
			continue;
		for (uint8_t g = 1; g <= BAS_FLOW_MIX_MAX_GAINS; g++) {
			if (mix_b(c, g) != b)
				continue;
			uint8_t bl = mix_b((uint8_t)(2 * p + 1), g), br = mix_b((uint8_t)(2 * p + 2), g);
			uint8_t vl = bl && bl != 0xFF ? SV(BAS_STORAGE_MIX_BASE, frame + bl) : 0;
			uint8_t vr = br && br != 0xFF ? SV(BAS_STORAGE_MIX_BASE, frame + br) : 0;
			return mode == 2 ? vr : mode == 3 ? vl : (uint8_t)((vl + vr + 1) / 2);
		}
	}
	return SV(BAS_STORAGE_MIX_BASE, frame + b);
}

uint8_t api_mix_value(uint8_t c, uint8_t g)  /* effective value of mixer.<active>.c.g */
{
	uint8_t b = mix_b(c, g);
	if (b == 0 || b == 0xFF || store_mix_position < 1 || store_mix_position > BAS_FLOW_MIX_MAX_POSITIONS)
		return 0;
	return mix_value((uint16_t)((store_mix_position - 1) * BAS_FLOW_MIX_MAX_POS_FRAME), b);
}

void api_set_mix_reg(void)
{
	if (!(hardware_dsp & (1u << BAS_TAS3108_1_RDY)))
		return;
	/* PORT: an unstored preset has position 0xFF; BASCOM then reads far beyond storage_mix
	 * and writes RAM contents as mixer coefficients. Skip invalid positions. */
	if (store_mix_position < 1 || store_mix_position > BAS_FLOW_MIX_MAX_POSITIONS)
		return;
	uint16_t count = (uint16_t)((store_mix_position - 1) * BAS_FLOW_MIX_MAX_POS_FRAME);
	for (uint8_t i = 1; i <= BAS_FLOW_MIX_MAX_REG; i++) {
		if (store_mix_changed & (1UL << i)) {
			for (uint8_t ii = 1; ii <= 4; ii++) {
				count++;
				/* cvt_long = storage_mix(count); PORT: with the mono overlay (#14) */
				uint16_t frame = (uint16_t)((store_mix_position - 1) * BAS_FLOW_MIX_MAX_POS_FRAME);
				double d = mix_value(frame, (uint8_t)(count - frame));
				d = d / 100;
				uint32_t v = dsp_math_to523(d);
				for (uint8_t k = 0; k < 4; k++)
					buffer_bytes[(ii - 1) * 4 + 1 + k] = (uint8_t)(v >> (8 * k));
			}
			i2c_device_addr = 0x68;  /* dsp_1_slave_adr_w */
			i2c_reg = BAS_LOOKUP_B(data_flow_mix_dsp_reg, i);
			i2c_size = BAS_FLOW_MIX_MAX_REG_SIZE;
			i2c_wbuffer(1);
		} else {
			count += 4;
		}
	}
	uint8_t mux = SV(BAS_STORAGE_MUX_BASE, store_mix_position);
#if BAS_MODUL_CT7302 > 0
	/* PORT: index checked; BASCOM reads beyond the table for invalid stored values */
	uint8_t ch = mux < modul_ct7302_tab_set_mux_ITEMS ? BAS_LOOKUP_B(modul_ct7302_tab_set_mux, mux) : 0xFF;
	if (ch < 0xFF)
		ct7302_20_set_mux(ch);
	if (PIN_READ(PIN_OPTION_SPDIF_4CH) == 0x01)
		ct7302_22_set_mux(0x03);  /* four channel mode: RX3 optical 2 */
#elif BAS_MODUL_SRC43X2 > 0
	uint8_t ch = mux < modul_src43x2_tab_set_mux_ITEMS ? BAS_LOOKUP_B(modul_src43x2_tab_set_mux, mux) : 0xFF;
	if (ch < 0xFF)
		src43x2_set_mux(ch);
#endif
	store_mix_changed = 0;
}

void api_restore_mix(void) { store_mix_changed = BAS_FLOW_MIX_MAX_CHANGED; }  /* done by api_mainloop */

/* --- input selection (issue #23): api_set/get/restore_in_input are declared in the original
 * without an implementation. The input is the mux of the active mixer position (the mux table
 * maps it to the S/PDIF receiver input). Events 45..48 (SOURCE.1..4.MUX in events.txt, never set
 * in the original) carry the mux of each position. --- */
#if BAS_MODUL_CT7302 > 0
#define MUX_TAB modul_ct7302_tab_set_mux
#define MUX_ITEMS modul_ct7302_tab_set_mux_ITEMS
#elif BAS_MODUL_SRC43X2 > 0
#define MUX_TAB modul_src43x2_tab_set_mux
#define MUX_ITEMS modul_src43x2_tab_set_mux_ITEMS
#endif

uint8_t api_in_input_valid(uint8_t v)
{
#ifdef MUX_TAB
	return v >= 1 && v < MUX_ITEMS && BAS_LOOKUP_B(MUX_TAB, v) != 0xFF;
#else
	(void)v;
	return 0;
#endif
}

uint8_t api_in_input_max(void)
{
	uint8_t n = 0;
	for (uint8_t v = 1; v < 16; v++)
		if (api_in_input_valid(v))
			n = v;
	return n;
}

void api_publish_in_input(void)
{
	for (uint8_t pos = 1; pos <= BAS_STORE_MUX_COUNT && pos <= 4; pos++)
		event_set_value((uint8_t)(BAS_EVENTS_FIELD_SOURCE_MUX + pos), SV(BAS_STORAGE_MUX_BASE, pos));
}

uint8_t api_get_in_input(void)
{
	uint8_t pos = store_mix_position;
	return pos >= 1 && pos <= BAS_STORE_MUX_COUNT ? SV(BAS_STORAGE_MUX_BASE, pos) : 0;
}

/* 0 = refused (invalid input or no valid mixer position) */
uint8_t api_set_in_input(uint8_t v)
{
	uint8_t pos = store_mix_position;
	if (!api_in_input_valid(v) || pos < 1 || pos > BAS_STORE_MUX_COUNT)
		return 0;
	if (SV(BAS_STORAGE_MUX_BASE, pos) != v) {
		api_set_out_mute_change();  /* the mux is set by api_set_mix_reg with the auto unmute */
		SV(BAS_STORAGE_MUX_BASE, pos) = v;
		if (api_store == 1)
			storage_flags++;
	}
	api_publish_in_input();
	return 1;
}

void api_restore_in_input(void)
{
	api_restore_mix();  /* sets the mux of the active position with the mixer registers */
	api_publish_in_input();
}

static void led_gruen_gelb_wait(uint8_t a)
{
	PIN_CLR(PIN_LED_GREEN);
	PIN_SET(PIN_LED_YELLOW);
	for (uint16_t i = 1; i <= a; i++) {  /* 16 bit: a = 255 must not wrap */
		PIN_TGL(PIN_LED_GREEN);
		PIN_TGL(PIN_LED_YELLOW);
		delay_ms(250);
	}
	PIN_SET(PIN_LED_GREEN);
	PIN_CLR(PIN_LED_YELLOW);
}

/* modc.32 sets 1 / 3: wait before and after remote out (250 ms steps) */
void api_startup_delay(void)
{
	if (events_field_value[BAS_EVENTS_FIELD_REMOTEOUT] == BAS_EVENTS_STATE_FALSE &&
	    events_field_value[BAS_EVENTS_FIELD_SYSTEM_SHUTDOWN] == BAS_EVENTS_STATE_FALSE) {
		uint8_t a = modc_sets[BAS_MAX_MODC];                 /* modc_set1(max_modc) */
		if (a > 0)
			led_gruen_gelb_wait(a);
#if FORGE_AUDIO
		PIN_SET(PIN_CPU_POWER_CTRL_OUT);  /* remote out: amplifiers on */
#endif
		event_set_value(BAS_EVENTS_FIELD_REMOTEOUT, BAS_EVENTS_STATE_TRUE);
		a = modc_sets[2 * BAS_MAX_MODC + BAS_MAX_MODC];      /* modc_set3(max_modc) */
		if (a > 0)
			led_gruen_gelb_wait(a);
	}
	PIN_CLR(PIN_LED_YELLOW);
}

void api_preset_load(uint8_t preset)
{
	if (preset == store_preset)
		return;
	if (preset >= BAS_MODUL_MAX_PRESET)  /* PORT: an invalid register value would index beyond the address tables */
		return;
	event_set_value(BAS_EVENTS_FIELD_SYSTEM_PRESET, preset);
	api_set_out_mute_change();
	store_preset = preset;
	store_load_preset();
	api_store = 0;
	for (uint8_t ch = 1; ch <= BAS_FLOW_MAX_IN_CHANNEL; ch++)
		api_set_in_vol(ch, api_get_in_vol(ch));
	for (uint8_t ch = 1; ch <= BAS_FLOW_MAX_SPDIF_CHANNEL; ch++)
		api_set_spdif_vol(ch, api_get_spdif_vol(ch));
	api_set_phase(api_get_phase());
#if BAS_FEATURE_INPUT_DELAY > 0
	for (uint8_t ch = 1; ch <= BAS_STORE_DELAY_IN_COUNT; ch++)
		api_set_delay_in(ch, api_get_delay_in(ch));
	dsp_ack_delay();
#endif
	for (uint8_t ch = 1; ch <= BAS_STORE_DELAY_COUNT; ch++)
		api_set_delay(ch, api_get_delay(ch));
	dsp_ack_delay();
	api_restore_in_input();  /* api_restore_mix + events 45..48 */
	modc_loudness_invalidate();  /* the preset load rewrote all biquads (M6) */
	for (uint8_t ch = 1; ch <= BAS_FLOW_MAX_OUT_CHANNEL; ch++)
		api_set_out_vol(ch, api_get_out_vol(ch));
	api_store = 1;
	api_startup_delay();  /* "first unmute must check for remote.out delay" */
	api_set_out_mute();   /* api_restore_out_mute */
	sys_init_preset_loaded();  /* PORT (#40): start commands after the first preset load */
	static const uint16_t ev[cmd_req_words_max] = {0x0104, 0x7072, 0x6C6F, 0x0000, 0x0000};
	uint8_t i = 0, v = store_preset;
	if (v >= 100) cmd_req_value[i++] = (char)('0' + v / 100);
	if (v >= 10) cmd_req_value[i++] = (char)('0' + v / 10 % 10);
	cmd_req_value[i++] = (char)('0' + v % 10);
	cmd_req_value[i] = 0;
	cmd_load_header(ev);
	cmd_sendevent();
}
