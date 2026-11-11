/*
 * Port of libs/api (functions.bas, mute/functions.bas, mix/functions.bas) and the load
 * sequence of modc_load_preset. License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "api.h"
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

void api_set_mix_position(uint8_t pos)
{
	if (store_mix_position != pos) {
		store_mix_position = pos;
		store_mix_changed = BAS_FLOW_MIX_MAX_CHANGED;
		api_set_out_mute_change();
		/* PORT: event "mixer.position" to the GUI (cmd_sendevent) follows with the events */
	}
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
				double d = SV(BAS_STORAGE_MIX_BASE, count);  /* cvt_long = storage_mix(count) */
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
	api_restore_mix();
	for (uint8_t ch = 1; ch <= BAS_FLOW_MAX_OUT_CHANNEL; ch++)
		api_set_out_vol(ch, api_get_out_vol(ch));
	api_store = 1;
	api_startup_delay();  /* "first unmute must check for remote.out delay" */
	api_set_out_mute();   /* api_restore_out_mute */
	static const uint16_t ev[cmd_req_words_max] = {0x0104, 0x7072, 0x6C6F, 0x0000, 0x0000};
	uint8_t i = 0, v = store_preset;
	if (v >= 100) cmd_req_value[i++] = (char)('0' + v / 100);
	if (v >= 10) cmd_req_value[i++] = (char)('0' + v / 10 % 10);
	cmd_req_value[i++] = (char)('0' + v % 10);
	cmd_req_value[i] = 0;
	cmd_load_header(ev);
	cmd_sendevent();
}
