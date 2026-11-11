/*
 * Port of libs/ctrl/modc/v20/functions.bas and subs/ctrl_* (preset, shutdown, mute, volumen,
 * logic, select, toggle, limiter, compare, calculator, spdif, mix_i, device_sync, gain,
 * gain_quad, gain_irrc, mainclock, ess9038pro): all block types of the original.
 * PORT: at sysinit the defaults are used without writing them to the EEPROM (see store_init).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "modc.h"
#include "api.h"
#include "bas_data.h"
#include "board.h"
#include "cmd.h"
#include "eram.h"
#include "events.h"
#include "flavour_data.h"
#include "i2c.h"
#include "iface.h"
#include "registers.h"
#include "store.h"
#include "tick.h"
#include "ir.h"

uint8_t modc_enable[MODC_N + 1], modc_type[MODC_N + 1];
uint8_t modc_flags[4 * MODC_N + 1], modc_events[4 * MODC_N + 1], modc_sets[4 * MODC_N + 1];
uint16_t modc_cfg_store[MODC_N + 1];
uint16_t modc_active_affect;
uint8_t modc_current_moc;
uint8_t modc_loadpreset, modc_cleanup;
static uint8_t modc_current_output;
void watchdog_kick(void);

#define M modc_current_moc
#define FLAG(k) modc_flags[((k) - 1) * MODC_N + M]
#define EVENT(k) modc_events[((k) - 1) * MODC_N + M]
#define SET(k) modc_sets[((k) - 1) * MODC_N + M]
#define OUT_VALUE events_field_value[BAS_EVENTS_FIELD_MODC_VALUE + M]  /* modc_output_value(n) */
#define CHG modc_is_changed[M]
#define BIT(v, n) (((v) >> (n)) & 1)
/* events_field_value(e) for an event number from the configuration; PORT: 0 for numbers
 * beyond the array (BASCOM reads whatever follows) */
static uint8_t EV(uint8_t e) { return e <= BAS_MAX_EVENTS ? events_field_value[e] : 0; }

static void bind(uint8_t event, uint8_t moc_bit, uint8_t on)
{
	if (event < 1 || event > BAS_MAX_EVENTS)
		return;
	if (on)
		events_bind_modc[event] |= 1UL << moc_bit;
	else
		events_bind_modc[event] &= ~(1UL << moc_bit);
}

void modc_chk_affect(void)
{
	modc_active_affect = 0;
	for (uint8_t i = 1; i <= MODC_N; i++) {
		if (modc_enable[i] == 0)
			continue;
		if (modc_type[i] == BAS_MODC_TYPE_PRESET) modc_active_affect |= 1u << BAS_ONPRESET;
		if (modc_type[i] == BAS_MODC_TYPE_VOLUMEN) modc_active_affect |= 1u << BAS_ONOUTVOL;
	}
}

void modc_set_run(uint8_t typ)
{
	for (uint8_t i = 1; i <= MODC_N; i++) {
		if (modc_enable[i] == 0)
			continue;
		if ((modc_type[i] == BAS_MODC_TYPE_PRESET && typ == BAS_ONPRESET) ||
		    (modc_type[i] == BAS_MODC_TYPE_VOLUMEN && typ == BAS_ONOUTVOL))
			modc_is_changed[i] |= 1u << BAS_MODC_BIT_RUN;
	}
}

void modc_request_preset(uint8_t preset)
{
	modc_loadpreset = preset;
	modc_cleanup |= 1u << BAS_CU_LOADPRESET;
}

uint8_t modc_shutdown_enabled(void)
{
	for (uint8_t i = 1; i <= MODC_N; i++)
		if (modc_enable[i] > 0 && modc_type[i] == BAS_MODC_TYPE_SHUTDOWN)
			return 1;
	return 0;
}

void ctrl_modc_init(void)
{
	uint8_t sysinit = registers[BAS_REG_STARTUP_STATE] == BAS_STARTUP_STATE_SYSINIT;
	for (uint8_t i = 1; i <= MODC_N; i++) {
		modc_enable[i] = sysinit ? BAS_LOOKUP_B(modc_enable_default, i - 1) : eram_rb(ERAM_EL(MODC_ENABLE_ERAM, i));
		modc_type[i] = sysinit ? BAS_LOOKUP_B(modc_type_default, i - 1) : eram_rb(ERAM_EL(MODC_TYPE_ERAM, i));
	}
	for (uint8_t i = 1; i <= 4 * MODC_N; i++) {
		modc_flags[i] = sysinit ? BAS_LOOKUP_B(modc_flags_default, i - 1) : eram_rb(ERAM_EL(MODC_FLAGS_ERAM, i));
		modc_events[i] = sysinit ? BAS_LOOKUP_B(modc_events_default, i - 1) : eram_rb(ERAM_EL(MODC_EVENTS_ERAM, i));
		modc_sets[i] = sysinit ? BAS_LOOKUP_B(modc_sets_default, i - 1) : eram_rb(ERAM_EL(MODC_SETS_ERAM, i));
	}
	for (M = 1; M <= MODC_N; M++) {
		if (modc_enable[M] > 0) {
			CHG = 0xFF;
			/* PORT: gain_quad enables the PORTF pin interrupt here; the port polls */
			for (uint8_t k = 1; k <= 4; k++)
				if (EVENT(k) > 0)
					bind(EVENT(k), (uint8_t)(M - 1), 1);
		} else {
			CHG = 0x00;
		}
	}
	modc_chk_affect();
	if (!(modc_active_affect & (1u << BAS_ONPRESET)))
		modc_request_preset(registers[BAS_REG_LOAD_PRESET]);
	else
		modc_set_run(BAS_ONPRESET);
}

static void modc_chk_events_changed(void)
{
	for (uint8_t e = 1; e <= BAS_MAX_EVENTS; e++) {
		if (events_is_changed[e] == 0)
			continue;
		events_is_changed[e] = 0;
		if (events_bind_modc[e] == 0)
			continue;
		for (uint8_t b = 0; b <= 31; b++)
			if (events_bind_modc[e] & (1UL << b))
				modc_is_changed[b + 1] |= 1u << BAS_MODC_BIT_EVENTS;
	}
}

static void modc_rebind_events(void)
{
	uint8_t mbit = (uint8_t)(M - 1);
	for (uint8_t e = 1; e <= BAS_MAX_EVENTS; e++)
		events_bind_modc[e] &= ~(1UL << mbit);
	if (modc_enable[M] > 0 && modc_type[M] > 0)
		for (uint8_t k = 1; k <= 4; k++)
			if (EVENT(k) > 0)
				bind(EVENT(k), mbit, 1);
}

/* --- configuration (cmd_modc) --- */

void modc_set_enable(uint8_t value)
{
	modc_enable[M] = value;
	eram_wb(ERAM_EL(MODC_ENABLE_ERAM, M), value);
	modc_rebind_events();
	modc_chk_affect();
	CHG |= 1u << BAS_MODC_BIT_CHANGED_ONOFF;
}

void modc_set_type(uint8_t value)
{
	if (modc_type[M] == value)
		return;
	if (BAS_LOOKUP_B(data_mods_versions, value & 0x3F) == 0)
		value = BAS_MODC_TYPE_EMPTY;
	modc_type[M] = value;
	eram_wb(ERAM_EL(MODC_TYPE_ERAM, M), value);
	if (value == BAS_MODC_TYPE_EMPTY) {
		modc_enable[M] = 0;
		eram_wb(ERAM_EL(MODC_ENABLE_ERAM, M), 0);
	}
	uint8_t a = M;
	for (uint8_t i = 1; i <= 4; i++) {
		modc_flags[a] = 0;
		modc_events[a] = 0;
		modc_sets[a] = 0;
		/* as the original: this clears event fields n, n+32, n+64, n+96 (not the output
		 * field 96+n only); kept for compatibility, see the upstream-gap issue */
		events_field_value[a] = 0;
		eram_wb(ERAM_EL(MODC_FLAGS_ERAM, a), 0);
		eram_wb(ERAM_EL(MODC_EVENTS_ERAM, a), 0);
		eram_wb(ERAM_EL(MODC_SETS_ERAM, a), 0);
		a = (uint8_t)(a + MODC_N);
	}
	if (value == BAS_MODC_TYPE_EMPTY)
		modc_chk_affect();
	else
		modc_rebind_events();
}

void modc_set_flags(uint8_t nr, uint8_t value)
{
	FLAG(nr) = value;
	eram_wb(ERAM_EL(MODC_FLAGS_ERAM, (nr - 1) * MODC_N + M), value);
	CHG |= 1u << BAS_MODC_BIT_FLAGS;
}

void modc_set_event(uint8_t nr, uint8_t value)
{
	CHG |= (uint8_t)(1u << (nr - 1));  /* modc_is_changed(n).modc_nr = 1 with the decremented nr */
	EVENT(nr) = value;
	eram_wb(ERAM_EL(MODC_EVENTS_ERAM, (nr - 1) * MODC_N + M), value);
	modc_rebind_events();
}

void modc_set_set(uint8_t nr, uint8_t value)
{
	SET(nr) = value;
	eram_wb(ERAM_EL(MODC_SETS_ERAM, (nr - 1) * MODC_N + M), value);
	CHG |= 1u << BAS_MODC_BIT_SETS;
}

/* --- block types --- */

static void out(uint8_t v) { event_set_value(modc_current_output, v); }

#if BAS_MODC_VERSION_PRESET == 20
static void modc_preset(void)
{
	if (CHG == 0)
		return;
	uint8_t a = store_preset;
	if (CHG & (1u << BAS_MODC_BIT_EVENTS)) {
		if (BIT(FLAG(1), 0) == 0) {
			a = (uint8_t)(EV(EVENT(4)) - 1);
		} else {
			a = 0;
			for (uint8_t i = 0; i <= (BAS_MODUL_MAX_PRESET == 4 ? 1 : 2); i++)
				if (EV(EVENT(i + 1)) > 0)
					a |= (uint8_t)(1u << i);
		}
	}
	a &= (uint8_t)(BAS_MODUL_MAX_PRESET - 1);
	modc_chk_affect();
	modc_request_preset(a);
	out(a);
}
#else
static void modc_preset(void) {}
#endif

static const uint16_t shutdown_event[cmd_req_words_max] = {0x0104, 0x7379, 0x7368, 0x0000, 0x0000};

#if BAS_MODC_VERSION_SHUTDOWN == 20
static void modc_shutdown(void)
{
	if (EV(EVENT(1)) > 0) {
		if (SET(2) > OUT_VALUE) {
			delay_ms(50);
			out((uint8_t)(OUT_VALUE + 1));
			CHG |= 1u << BAS_MODC_BIT_EVENTS;
		} else {
#if BAS_FEATURE_MCU_TEMP > 0
			uint16_t t = events_adc_values[3];  /* peak MCU temperature since the start */
			if (t > events_adc_McuMaxTemp100C) errorcount(BAS_ERRORCOUNT_FW_MCUHIGHTEMP100C);
			else if (t > events_adc_McuMaxTemp95C) errorcount(BAS_ERRORCOUNT_FW_MCUHIGHTEMP95C);
			else if (t > events_adc_McuMaxTemp90C) errorcount(BAS_ERRORCOUNT_FW_MCUHIGHTEMP90C);
			else if (t > events_adc_McuMaxTemp85C) errorcount(BAS_ERRORCOUNT_FW_MCUHIGHTEMP85C);
#endif
			cmd_req_value[0] = 'o'; cmd_req_value[1] = 'k'; cmd_req_value[2] = 0;
			cmd_load_header(shutdown_event);
			cmd_sendevent();
			out(0x00);
			CHG = 0x00;
			api_set_out_mute_prio(api_mute_out_prio_system, api_mute_out_on);
			power_shutdown(SET(3), SET(4));
		}
	} else {
		out(0x00);
		CHG = 0x00;
	}
}
#else
static void modc_shutdown(void) {}
#endif

#if BAS_MODC_VERSION_MUTE == 20
static void modc_mute(void)
{
	if (modc_enable[M] == 0) {
		api_set_out_mute_prio(api_mute_out_prio_modc, api_mute_out_off);
		out(0);
	} else if (EVENT(1) > 0) {
		uint8_t v = EV(EVENT(1)) > 0 ? FLAG(1) : api_mute_out_off;
		api_set_out_mute_prio(api_mute_out_prio_modc, v);
		out(v);
	}
}
#else
static void modc_mute(void) {}
#endif

static const uint16_t volumen_event[cmd_req_words_max] = {0x0104, 0x6761, 0x6F75, 0x7374, 0x0000};

#if BAS_MODC_VERSION_VOLUMEN == 20
static void modc_volumen(void)
{
	if (modc_enable[M] == 0)
		return;
	uint8_t b[14];
	for (uint8_t i = 1; i <= 8; i++)
		b[i] = api_get_out_vol(i);
	b[9] = (uint8_t)(255 - EV(EVENT(1)));   /* attenuation odd channels */
	b[10] = (uint8_t)(255 - EV(EVENT(2)));  /* attenuation even channels */
	for (uint8_t i = 1; i <= 8; i++) {
		uint8_t att = (i & 1) ? b[9] : b[10];
		b[i] = att >= b[i] ? 0 : (uint8_t)(b[i] - att);
	}
	if (EVENT(3) > 0) {  /* correction around 0x10 for the outputs in set 3 */
		uint8_t v = EV(EVENT(3));
		for (uint8_t i = 1; i <= 8; i++) {
			if (!BIT(SET(3), i - 1))
				continue;
			if (v > BAS_MODC_CORRECTION_MIDDLE) {
				uint8_t d = (uint8_t)(v - BAS_MODC_CORRECTION_MIDDLE);
				b[i] = (uint8_t)(255 - b[i]) > d ? (uint8_t)(b[i] + d) : 255;
			} else {
				uint8_t d = (uint8_t)(BAS_MODC_CORRECTION_MIDDLE - v);
				b[i] = b[i] > d ? (uint8_t)(b[i] - d) : 0;
			}
		}
	}
	if (EVENT(4) > 0) {  /* balance around 0x40 for the outputs in set 4 */
		uint8_t v = EV(EVENT(4));
		uint8_t left = 0, right = 0;
		if (v > BAS_MODC_BALANCE_MIDDLE)
			right = (uint8_t)(v - BAS_MODC_BALANCE_MIDDLE);
		else
			left = (uint8_t)(BAS_MODC_BALANCE_MIDDLE - v);
		for (uint8_t i = 0; i <= 7; i++) {
			if (!BIT(SET(4), i))
				continue;
			uint8_t d = (i & 1) ? right : left;
			b[i + 1] = b[i + 1] > d ? (uint8_t)(b[i + 1] - d) : 0;
		}
	}
	api_store = 0;
	for (uint8_t i = 0; i <= 7; i++)
		if (BIT(FLAG(1), i))
			api_set_out_vol_hw((uint8_t)(i + 1), b[i + 1]);
	api_store = 1;
	char *p = cmd_req_value;
	*p++ = '[';
	for (uint8_t i = 0; i <= 7; i++) {
		if (BIT(FLAG(1), i)) {
			uint8_t v = b[i + 1];
			if (v >= 100) *p++ = (char)('0' + v / 100);
			if (v >= 10) *p++ = (char)('0' + v / 10 % 10);
			*p++ = (char)('0' + v % 10);
		} else {
			*p++ = '-';
			*p++ = '1';
		}
		if (i < 7)
			*p++ = ',';
	}
	*p++ = ']';
	*p = 0;
	cmd_load_header(volumen_event);
	cmd_sendevent();
}
#else
static void modc_volumen(void) {}
#endif

static void modc_logic(void)
{
	uint8_t v = 0;
	for (uint8_t i = 0; i <= 3; i++)
		if (EVENT(i + 1) > 0 && EV(EVENT(i + 1)) > 0)
			v |= (uint8_t)(1u << i);
	v ^= FLAG(1) & 0x0F;
	uint8_t e = 0;
	for (uint8_t i = 4; i <= 7; i++) {
		e = BIT(FLAG(1), i) == 0 ? (v & SET(i - 3)) : (v ^ SET(i - 3));
		if (e > 0)
			break;
	}
	out(e > 0 ? 0x01 : 0x00);
}

static void modc_select(void)
{
	if (EVENT(3) == 0)
		return;
	uint8_t a = (uint8_t)((FLAG(1) & 0x03) << 1);
	if (EV(EVENT(3)) > 0)
		a |= 1;
	uint8_t e;
	switch (a) {
	case 0: case 4: e = EVENT(1); if (e > 0) out(EV(e)); break;
	case 1: case 3: e = EVENT(2); if (e > 0) out(EV(e)); break;
	case 2: case 6: e = SET(1); if (e > 0) out(e); break;
	case 5: case 7: e = SET(2); if (e > 0) out(e); break;
	}
}

static void modc_toggle(void)
{
	if (modc_enable[M] == 0) {
		out(0);
	} else if ((CHG & (1u << BAS_MODC_BIT_EVENTS)) && EVENT(1) > 0 && EV(EVENT(1)) == 0) {
		out(OUT_VALUE ^ 0x01);
	}
}

static void modc_limiter(void)
{
	if (EVENT(1) == 0)
		return;
	uint8_t a = EV(EVENT(1));
	if (a < SET(2)) a = SET(2);
	if (a > SET(1)) a = SET(1);
	double d = 100.0 / (double)(uint8_t)(SET(1) - SET(2));
	d *= (double)(uint8_t)(a - SET(2));
	if (BIT(FLAG(1), 7))
		d = 100.0 - d;
	d *= (double)(uint8_t)(SET(3) - SET(4)) / 100.0;
	/* PORT: set1 = set2 divides by zero (BASCOM: undefined value); result 0 here */
	uint8_t v = SET(1) == SET(2) ? 0 : (uint8_t)(int32_t)d;
	v = (uint8_t)(v + SET(4));
	if (v > SET(3))
		v = SET(3);
	out(v);
}

static void modc_compare(void)
{
	uint8_t b1 = EV(EVENT(1)), b2 = EV(EVENT(2)), b3 = EV(EVENT(3)), a = FLAG(1), e = 0;
	if (BIT(a, 0) && b3 >= b1) e++;
	if (BIT(a, 1) && b3 <= b1) e++;
	if (BIT(a, 2) && b3 >= b2) e++;
	if (BIT(a, 3) && b3 <= b2) e++;
	if (BIT(a, 4) && b3 >= SET(3)) e++;
	if (BIT(a, 5) && b3 <= SET(3)) e++;
	out(e > 0 ? b2 : b1);
}

static void modc_calculator(void)
{
	uint8_t a = FLAG(2), v = 0;
	if (BIT(a, 5) == 0) {
		if (EVENT(1) > 0)
			v = EV(EVENT(1));
	} else {
		v = SET(1);
	}
	uint16_t w = v;
	FLAG(2) |= 0x80;
	for (uint8_t k = 2; k <= 4; k++) {
		a = FLAG(k);
		if (!BIT(a, 7))
			continue;
		v = 0;
		if (BIT(a, 6) == 0) {
			if (EVENT(k) > 0)
				v = EV(EVENT(k));
		} else {
			v = SET(k);
		}
		if (BIT(a, 0)) w = (uint16_t)(w * v);
		if (BIT(a, 1)) w = v ? (uint16_t)(w / v) : 0xFFFF;  /* PORT: BASCOM word / 0 */
		if (BIT(a, 2)) w = (uint16_t)(w + v);
		if (BIT(a, 3)) w = (uint16_t)(w - v);
	}
	out((uint8_t)w);
}

static void modc_spdif(void)
{
#if BAS_MODC_VERSION_SPDIF == 20 && defined(ERAM_CT7302_REGISTER_OPTIONS)
	eram_wb(ERAM_CT7302_REGISTER_OPTIONS, FLAG(1));
#endif
}

static void modc_mix_i(void)
{
	uint8_t v = 1;
	for (uint8_t i = 1; i <= 4; i++)
		if (EVENT(i) > 0 && EV(EVENT(i)) > 0)
			v = i;
	out(v);
	api_set_mix_position(v);
}

static const uint16_t block_event[cmd_req_words_max] = {0xFF03, 0x6576, 0x626C, 0x0000, 0x0000};

/* sends "event.block.N=[a,b,c,d]" to the slave port about every 480 passes or on a change */
static void modc_device_sync(void)
{
	if (modc_cfg_store[M] == 0) {
		modc_cfg_store[M] = 480;
		CHG |= 1u << BAS_MODC_BIT_RUN;
	}
	modc_cfg_store[M]--;
	if (CHG == 0)
		return;
	uint8_t v = (uint8_t)((FLAG(1) & 0x03) + 1);
	uint8_t ch = buart_channel;
	buart_channel = iface_buart_channel[iface_ch_slave];
	cmd_load_header(block_event);
	REQ_B(8) = cmd_req_number;
	REQ_B(7) = v;
	cmd_answer_header();
	out_chr('[');
	for (uint8_t i = 1; i <= 4; i++) {
		out_u16(EVENT(i) > 0 ? EV(EVENT(i)) : 0);
		if (i < 4)
			out_chr(',');
	}
	out_chr(']');
	out_crlf();
	buart_channel = ch;
}

/* flags1 bits 3..7: log10, log10_inv, alog10, alog10_inv curves, invert */
static uint8_t curve(uint8_t a)
{
	uint8_t f = FLAG(1);
	if (BIT(f, 3)) a = BAS_LOOKUP_B(modc_log10, a);
	if (BIT(f, 4)) a = BAS_LOOKUP_B(modc_log10_inv, a);
	if (BIT(f, 5)) a = BAS_LOOKUP_B(modc_alog10, a);
	if (BIT(f, 6)) a = BAS_LOOKUP_B(modc_alog10_inv, a);
	if (BIT(f, 7)) a = (uint8_t)(255 - a);
	return a;
}

/* pot (ADCA channel 1/2 by flag1 bit 0) scaled between set2 (low, x256) and set1 (high),
 * curve, then reduced to set3 steps. Word arithmetic as BASCOM (wraps below set2). */
static void modc_gain(void)
{
	if (CHG & (1u << BAS_MODC_BIT_CHANGED_ONOFF))
		modc_cfg_store[M] = 0;
	uint16_t w1 = (uint16_t)((BIT(FLAG(1), 0) == 0 ? events_adc_values[1] : events_adc_values[2]) << 5);
	if (w1 == modc_cfg_store[M])
		return;
	modc_cfg_store[M] = w1;
	uint16_t w2 = (uint16_t)(SET(2) << 8);
	uint8_t span = (uint8_t)(SET(1) - SET(2));
	uint8_t step = SET(3) ? (uint8_t)(255 / SET(3)) : 0;
	if (span == 0 || step == 0)  /* PORT: division by zero in the original (undefined) */
		return;
	uint8_t a = (uint8_t)((uint16_t)(w1 - w2) / span);
	a = (uint8_t)(curve(a) / step);
	if (OUT_VALUE != a)
		out(a);
}

/* rotary encoder (timer_tce0_value): sets 1..4 = start value, step, max, min */
static void modc_gain_quad(void)
{
	if (CHG) {
		if (CHG & (1u << BAS_MODC_BIT_CHANGED_ONOFF))
			CHG |= 1u << BAS_MODC_BIT_SETS;
		if (CHG & (1u << BAS_MODC_BIT_SETS)) {
			if (SET(1) > 0) timer_tce0_value = SET(1);
			if (SET(2) > 0) timer_tce0_factor = SET(2);
			if (SET(3) > 0) timer_tce0_max = SET(3);
			if (SET(4) > 0) timer_tce0_min = SET(4);
		}
	}
	int16_t v = timer_tce0_value;
	if ((int16_t)modc_cfg_store[M] != v) {
		modc_cfg_store[M] = (uint16_t)v;
		out(curve((uint8_t)v));
	}
}

/* IR remote volume: enable switches the IR master on (setup register), flag2 = IR setup bits,
 * sets = start value, step, max, min; output also to the ir.volume event */
static void modc_gain_irrc(void)
{
	if (CHG) {
		if (CHG & (1u << BAS_MODC_BIT_CHANGED_ONOFF)) {
			uint8_t a = registers[BAS_REG_EXT_IRSETUP];
			if (modc_enable[M] == 0)
				a &= (uint8_t)~(1u << BAS_IRRECEIVER_STATE_ON);
			else
				a |= 1u << BAS_IRRECEIVER_STATE_ON;
			set_register(BAS_REG_EXT_IRSETUP, a);
			CHG |= (1u << BAS_MODC_BIT_FLAGS) | (1u << BAS_MODC_BIT_SETS);
		}
		if (CHG & (1u << BAS_MODC_BIT_FLAGS))
			set_register(BAS_REG_EXT_IRSETUP, (uint8_t)((FLAG(2) & 0x3F) + (registers[BAS_REG_EXT_IRSETUP] & 0xC0)));
		if (CHG & (1u << BAS_MODC_BIT_SETS)) {
			if (SET(1) > 0) irreceiver_value = SET(1);
			if (SET(2) > 0) irreceiver_factor = SET(2);
			if (SET(3) > 0) irreceiver_value_max = SET(3);
			if (SET(4) > 0) irreceiver_value_min = SET(4);
		}
	}
	if ((int16_t)modc_cfg_store[M] != irreceiver_value) {
		modc_cfg_store[M] = (uint16_t)irreceiver_value;
		uint8_t a = curve((uint8_t)irreceiver_value);
		out(a);
		event_set_value(BAS_EVENTS_FIELD_IR_VOLUME, a);
	}
}
/* main clock: sets 1/2 = fine tune (integer), set 3 = index into data_modc_mainclock; stored in
 * the EEPROM, used by si5338 at the next start. Written only when changed, as the original. */
static void modc_mainclock(void)
{
#if BAS_MODC_VERSION_MAINCLOCK == 20
	uint16_t fine = (uint16_t)(SET(1) | SET(2) << 8);
	if (eram_rw(ERAM_SI5338_CUSTOM_MCLK_FINE_TUNE) != fine)
		eram_ww(ERAM_SI5338_CUSTOM_MCLK_FINE_TUNE, fine);
	uint8_t i = SET(3);
	if (i >= sizeof data_modc_mainclock / 4)  /* PORT: the original reads beyond the table */
		return;
	uint32_t clk = (uint32_t)BAS_LOOKUP_W(data_modc_mainclock, 2 * i) |
	               (uint32_t)BAS_LOOKUP_W(data_modc_mainclock, 2 * i + 1) << 16;
	uint16_t a = ERAM_EL(SI5338_OUT_CLOCK_ERAM, BAS_SI5338_MAX_DRIVERS);
	if (eram_rd(a) != clk)
		eram_wd(a, clk);
#endif
}

/* ES9038Pro: set 1 = system mute register value (filter/mute bits); default when disabled */
static void modc_ess9038pro(void)
{
#if BAS_MODC_VERSION_ESS9038PRO == 20
	if (CHG == 0)
		return;
	if (CHG & (1u << BAS_MODC_BIT_CHANGED_ONOFF))
		CHG |= 1u << BAS_MODC_BIT_SETS;
	if (!(CHG & (1u << BAS_MODC_BIT_SETS)))
		return;
	uint8_t a;
	if (modc_enable[M] == 0) {
		SET(1) = BAS_ES90X8PRO_FB_SYSMUTE_INIT;
		eram_wb(ERAM_EL(MODC_SETS_ERAM, M), BAS_ES90X8PRO_FB_SYSMUTE_INIT);
		a = BAS_ES90X8PRO_FB_SYSMUTE_INIT;
	} else {
		a = SET(1);
	}
	eram_wb(ERAM_ES90X8PRO_FB_SYSMUTE_ERAM, a);
	out(a);
#endif
}

void ctrl_modc_mainloop(void)
{
	for (M = 1; M <= MODC_N; M++) {
		modc_current_output = (uint8_t)(BAS_EVENTS_FIELD_MODC_VALUE + M);
		watchdog_kick();
		uint8_t type = modc_type[M];
		if (type == BAS_MODC_TYPE_EMPTY)
			continue;
		if (!((CHG & (1u << BAS_MODC_BIT_CHANGED_ONOFF)) || modc_enable[M] > 0))
			continue;
		modc_chk_events_changed();
		switch (type) {
		case BAS_MODC_TYPE_DEVICE_SYNC: modc_device_sync(); break;
		case BAS_MODC_TYPE_GAIN:        modc_gain(); break;
		case BAS_MODC_TYPE_GAIN_QUAD:   modc_gain_quad(); break;
		case BAS_MODC_TYPE_GAIN_IRRC:   modc_gain_irrc(); break;
		default:
			if (CHG == 0)
				break;
			switch (type) {
			case BAS_MODC_TYPE_PRESET:     modc_preset(); break;
			case BAS_MODC_TYPE_SHUTDOWN:   modc_shutdown(); break;
			case BAS_MODC_TYPE_MAINCLOCK:  modc_mainclock(); break;
			case BAS_MODC_TYPE_SPDIF:      modc_spdif(); break;
			case BAS_MODC_TYPE_ESS9038PRO: modc_ess9038pro(); break;
			case BAS_MODC_TYPE_VOLUMEN:    modc_volumen(); break;
			case BAS_MODC_TYPE_MUTE:       modc_mute(); break;
			case BAS_MODC_TYPE_MIX_I:      modc_mix_i(); break;
			case BAS_MODC_TYPE_LOGIC:      modc_logic(); break;
			case BAS_MODC_TYPE_SELECT:     modc_select(); break;
			case BAS_MODC_TYPE_TOGGLE:     modc_toggle(); break;
			case BAS_MODC_TYPE_LIMITER:    modc_limiter(); break;
			case BAS_MODC_TYPE_COMPARE:    modc_compare(); break;
			case BAS_MODC_TYPE_CALCULATOR: modc_calculator(); break;
			}
		}
		watchdog_kick();
		if (type != BAS_MODC_TYPE_SHUTDOWN)
			CHG = 0x00;
	}
	if (modc_cleanup & (1u << BAS_CU_LOADPRESET)) {
		api_preset_load(modc_loadpreset);
		modc_cleanup &= (uint8_t)~(1u << BAS_CU_LOADPRESET);
	}
}
