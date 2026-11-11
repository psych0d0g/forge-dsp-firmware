/*
 * Port of libs/extender/ir-receiver (functions.bas, NEC/, RC6/, SAMSUNG/, CUSTOM/) and the
 * capture parts of libs/ctrl/timer (timer_TCC1_int) and libs/interface/V40/3 (iface_ch3_rx_int).
 * At sysinit setup register and custom codes are written only with the factory reset (factory.h).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "ir.h"
#include "cmd.h"
#include "eram.h"
#include "events.h"
#include "modc.h"
#include "registers.h"
#include "factory.h"
#include "store.h"

volatile uint8_t irreceiver_state;
volatile uint32_t irreceiver_buffer[IR_MAX_BUFFER + 1];
uint16_t irreceiver_frames[4];
int16_t irreceiver_value;
uint8_t irreceiver_value_min, irreceiver_value_max, irreceiver_factor;
uint8_t irreceiver_analyze_echo;
uint16_t irreceiver_custom[4][IR_CUSTOM_MAX + 1];
static uint16_t irreceiver_counter;
static uint8_t irreceiver_count0, irreceiver_count1, irreceiver_error, irreceiver_bit, irreceiver_frame;
static uint16_t irreceiver_pulse, irreceiver_space;
static int16_t irreceiver_bal, irreceiver_bass;
static uint8_t irreceiver_mute, irreceiver_preset;
static uint16_t nec_frames[4];

#define ST_BUFFER_USED 0
#define ST_CUSTOM 0
#define ST_NEC 1
#define ST_RC6 2
#define ST_SAMSUNG 3
#define ST_ON BAS_IRRECEIVER_STATE_ON
#define header irreceiver_frames[1]
#define address irreceiver_frames[2]
#define command irreceiver_frames[3]
#define COUNTER_INIT 192
#define MAX_BITS (IR_MAX_BUFFER * 4 * 8)

void irreceiver_init(void)
{
	/* sysinit: setup register to irreceiver_setup_default (EEPROM only with the factory reset) */
	if (factory_write())
		set_register(BAS_REG_EXT_IRSETUP, 0x00);
	else if (sysinit_active())
		registers[BAS_REG_EXT_IRSETUP] = 0x00;
	irreceiver_analyze_echo = 1;
	irreceiver_value_min = 0x00;
	irreceiver_value_max = 0xFF;
	irreceiver_factor = 0x01;
	irreceiver_bal = BAS_MODC_BALANCE_MIDDLE;
	irreceiver_mute = 0;
	irreceiver_preset = 0;
}

void ir_custom_init(void)
{
	if (factory_write())
		for (uint8_t i = 1; i <= IR_CUSTOM_MAX; i++)
			ir_custom_set(i, 0xFFFF, 0xFFFF, 0xFFFF);  /* irreceiver_custom_init */
	for (uint8_t i = 1; i <= IR_CUSTOM_MAX; i++) {
		irreceiver_custom[1][i] = eram_rw(ERAM_EL(IRRECEIVER_CUSTOM_HEADER_ERAM, i));
		irreceiver_custom[2][i] = eram_rw(ERAM_EL(IRRECEIVER_CUSTOM_ADDRESS_ERAM, i));
		irreceiver_custom[3][i] = eram_rw(ERAM_EL(IRRECEIVER_CUSTOM_COMMAND_ERAM, i));
	}
}

void ir_custom_set(uint8_t i, uint16_t h, uint16_t a, uint16_t c)
{
	irreceiver_custom[1][i] = h;
	irreceiver_custom[2][i] = a;
	irreceiver_custom[3][i] = c;
	eram_ww(ERAM_EL(IRRECEIVER_CUSTOM_HEADER_ERAM, i), h);
	eram_ww(ERAM_EL(IRRECEIVER_CUSTOM_ADDRESS_ERAM, i), a);
	eram_ww(ERAM_EL(IRRECEIVER_CUSTOM_COMMAND_ERAM, i), c);
}

/* --- reading the sample buffer --- */

static void read_incr(void)
{
	irreceiver_count0++;
	if (irreceiver_count0 == IR_MAX_COUNT0) {
		irreceiver_count0 = 0;
		irreceiver_count1++;
	}
}

static uint8_t sample(void) { return (irreceiver_buffer[irreceiver_count1] >> irreceiver_count0) & 1; }

/* pulse = samples at 0 (IR active), space = following samples at 1 */
static void read_impuls(void)
{
	irreceiver_pulse = 0;
	irreceiver_space = 0;
	if (irreceiver_count1 > IR_MAX_BUFFER)
		return;
	while (sample() == 0) {
		irreceiver_pulse++;
		read_incr();
		if (irreceiver_count1 > IR_MAX_BUFFER)
			return;
	}
	while (sample() == 1) {
		irreceiver_space++;
		read_incr();
		if (irreceiver_count1 > IR_MAX_BUFFER)
			return;
	}
}

static uint8_t in(uint16_t v, uint16_t lo, uint16_t hi) { return v >= lo && v <= hi; }

static uint8_t write_bit(uint8_t v, uint8_t reload)
{
	if (irreceiver_frame > 3) {
		irreceiver_error++;
		return 1;
	}
	irreceiver_bit--;
	if (v)
		irreceiver_frames[irreceiver_frame] |= (uint16_t)(1u << irreceiver_bit);
	else
		irreceiver_frames[irreceiver_frame] &= (uint16_t)~(1u << irreceiver_bit);
	if (irreceiver_bit == 0) {
		irreceiver_frame++;
		irreceiver_bit = reload;
	}
	return 0;
}

/* NEC and Samsung: pulse 6..12, space 6..12 = 0, 23..28 = 1, longer = end */
static void pulse_distance(void)
{
	irreceiver_bit = 16;
	irreceiver_frame = 2;
	do {
		read_impuls();
		uint8_t err = 0, hl = 0;
		if (!in(irreceiver_pulse, 6, 12)) {
			err = 1;
			irreceiver_error++;
		}
		if (in(irreceiver_space, 6, 12)) {
		} else if (in(irreceiver_space, 23, 28)) {
			hl = 1;
		} else if (in(irreceiver_space, 29, MAX_BITS)) {
			err = 1;  /* end of the frame */
		} else {
			err = 1;
			irreceiver_error++;
		}
		if (err)
			break;
		if (write_bit(hl, 16))
			break;
	} while (irreceiver_count1 <= IR_MAX_BUFFER);
}

static void ir_nec_analyze(void)
{
	if (irreceiver_space > 30 && irreceiver_space < 37) {  /* repeat code */
		for (uint8_t i = 1; i <= 3; i++)
			irreceiver_frames[i] = nec_frames[i];
		if (irreceiver_frames[3] == 0x0000)
			irreceiver_error++;
	} else {
		pulse_distance();
		if (irreceiver_error == 0)
			for (uint8_t i = 1; i <= 3; i++)
				nec_frames[i] = irreceiver_frames[i];
	}
}

/* RC6: Manchester code, half bits short 5..9, long 11..16, toggle 18..22 */
#define RC6_HIGHLOW 0
#define RC6_INTOGGLE 1
#define RC6_PULSE_LONG 2
#define RC6_SPACE_LONG 3
#define RC6_PULSE_TOGGLE 4
#define RC6_SPACE_TOGGLE 5
#define RC6_PULSE_START 6
#define RC6_ERROR 7
static uint8_t rc6;
#define RC6_SET(b, v) (rc6 = (uint8_t)((v) ? (rc6 | (1u << (b))) : (rc6 & ~(1u << (b)))))

static void rc6_bit(void)
{
	if (write_bit(rc6 & 1, 8))
		rc6 |= 1u << RC6_ERROR;
}

static void rc6_state_machine(void)
{
	switch (rc6) {
	case 0x00: case 0x01: case 0x0D: rc6_bit(); break;
	case 0x04: rc6_bit(); RC6_SET(RC6_HIGHLOW, 1); rc6_bit(); break;
	case 0x06: case 0x07: rc6_bit(); RC6_SET(RC6_INTOGGLE, 0); break;
	case 0x08: rc6_bit(); RC6_SET(RC6_INTOGGLE, 1); break;
	case 0x09: rc6_bit(); RC6_SET(RC6_HIGHLOW, 0); break;
	case 0x0C: rc6_bit(); RC6_SET(RC6_HIGHLOW, 1); rc6_bit(); RC6_SET(RC6_HIGHLOW, 0); break;
	case 0x18: rc6_bit(); RC6_SET(RC6_HIGHLOW, 1); rc6_bit(); break;
	case 0x1A: rc6_bit(); RC6_SET(RC6_HIGHLOW, 1); rc6_bit(); RC6_SET(RC6_HIGHLOW, 0); RC6_SET(RC6_INTOGGLE, 0); break;
	case 0x21: rc6_bit(); RC6_SET(RC6_INTOGGLE, 1); break;
	case 0x24: rc6_bit(); RC6_SET(RC6_HIGHLOW, 1); rc6_bit(); RC6_SET(RC6_HIGHLOW, 0); RC6_SET(RC6_INTOGGLE, 1); break;
	case 0x25: rc6_bit(); RC6_SET(RC6_HIGHLOW, 0); break;
	case 0x30: rc6_bit(); RC6_SET(RC6_HIGHLOW, 1); rc6_bit(); RC6_SET(RC6_HIGHLOW, 0); break;
	case 0x49: break;
	default: rc6 |= 1u << RC6_ERROR;
	}
}

static void ir_rc6_analyze(void)
{
	irreceiver_bit = 5;
	irreceiver_frame = 1;
	rc6 = 1u << RC6_HIGHLOW;
	do {
		read_impuls();
		rc6 &= 0x03;
		if (in(irreceiver_pulse, 5, 9)) {
		} else if (in(irreceiver_pulse, 11, 16)) {
			rc6 |= 1u << RC6_PULSE_LONG;
		} else if (in(irreceiver_pulse, 18, 22)) {
			rc6 |= 1u << RC6_PULSE_TOGGLE;
		} else if (in(irreceiver_pulse, 38, 46)) {
			rc6 |= 1u << RC6_PULSE_START;
		} else {
			rc6 |= 1u << RC6_ERROR;
			irreceiver_error++;
		}
		if (in(irreceiver_space, 5, 9)) {
		} else if (in(irreceiver_space, 11, 16)) {
			rc6 |= 1u << RC6_SPACE_LONG;
		} else if (in(irreceiver_space, 18, 22)) {
			rc6 |= 1u << RC6_SPACE_TOGGLE;
		} else if (in(irreceiver_space, 23, MAX_BITS)) {
			rc6 |= 1u << RC6_SPACE_LONG;
		} else {
			rc6 |= 1u << RC6_ERROR;
			irreceiver_error++;
		}
		if (rc6 & (1u << RC6_ERROR))
			break;
		rc6_state_machine();
		if (rc6 & (1u << RC6_ERROR))
			break;
	} while (irreceiver_count1 <= IR_MAX_BUFFER);
	header &= 0xFFFE;
}

/* --- actions --- */

static void incr_gain(void)
{
	irreceiver_value = (int16_t)(irreceiver_value + irreceiver_factor);
	if (irreceiver_value > irreceiver_value_max) irreceiver_value = irreceiver_value_max;
}

static void decr_gain(void)
{
	irreceiver_value = (int16_t)(irreceiver_value - irreceiver_factor);
	if (irreceiver_value < irreceiver_value_min) irreceiver_value = irreceiver_value_min;
}

static void balance(int8_t d)
{
	irreceiver_bal = (int16_t)(irreceiver_bal + d);
	if (irreceiver_bal < 1) irreceiver_bal = 1;
	if (irreceiver_bal > 0x7F) irreceiver_bal = 0x7F;
	event_set_value(BAS_EVENTS_FIELD_IR_BALANCE, (uint8_t)irreceiver_bal);
}

static void bass(int8_t d)  /* as the original: "incr" decrements and vice versa */
{
	irreceiver_bass = (int16_t)(irreceiver_bass + d);
	if (irreceiver_bass < 0) irreceiver_bass = 0;
	if (irreceiver_bass > 24) irreceiver_bass = 24;
	event_set_value(BAS_EVENTS_FIELD_IR_BASS, (uint8_t)irreceiver_bass);
}

static void mute_unmute(void)
{
	if (irreceiver_counter == 0) {
		irreceiver_mute ^= 1;
		event_set_value(BAS_EVENTS_FIELD_IR_MUTE, irreceiver_mute);
		irreceiver_counter = COUNTER_INIT;
	}
}

static void set_preset(uint8_t v)
{
	if (irreceiver_counter == 0) {
		if (v < BAS_MODUL_MAX_PRESET && !(modc_cleanup & (1u << BAS_CU_LOADPRESET))) {
			irreceiver_preset = v;
			event_set_value(BAS_EVENTS_FIELD_IR_SWITCH0, v & 1);
			event_set_value(BAS_EVENTS_FIELD_IR_SWITCH1, (v >> 1) & 1);
			event_set_value(BAS_EVENTS_FIELD_IR_SWITCH2, (v >> 2) & 1);
		}
		irreceiver_counter = COUNTER_INIT;
	}
}

static void ir_custom_execute(void)
{
	for (uint8_t i = 1; i <= IR_CUSTOM_MAX; i++) {
		if (header != irreceiver_custom[1][i] || address != irreceiver_custom[2][i] || command != irreceiver_custom[3][i])
			continue;
		switch (i) {
		case 1: mute_unmute(); break;
		case 2: break;
		case 3: incr_gain(); break;
		case 4: decr_gain(); break;
		case 5: balance(-1); break;
		case 6: balance(1); break;
		case 7: bass(-1); break;
		case 8: bass(1); break;
		case 9: set_preset((uint8_t)(store_preset + 1)); break;
		case 10: set_preset((uint8_t)(store_preset - 1)); break;
		default: set_preset((uint8_t)(i - 11)); break;
		}
		break;
	}
}

static void ir_nec_execute(void)
{
	switch (command) {
	case 0x10CC: balance(-1); break;
	case 0x7ACC: mute_unmute(); break;
	case 0xB0CC: decr_gain(); break;
	case 0xD0CC: incr_gain(); break;
	case 0xE0CC: balance(1); break;
	}
}

static void ir_rc6_execute(void)
{
	if (command >= 0x0001 && command <= 0x0008) {
		set_preset((uint8_t)(command - 1));
		return;
	}
	switch (command) {
	case 0x000D: mute_unmute(); break;
	case 0x0010: incr_gain(); break;
	case 0x0011: decr_gain(); break;
	case 0x001A: balance(1); break;
	case 0x001B: balance(-1); break;
	case 0x0020: set_preset((uint8_t)(store_preset + 1)); break;
	case 0x0021: set_preset((uint8_t)(store_preset - 1)); break;
	}
}

static const uint16_t analyze_event[cmd_req_words_max] = {0x0104, 0x6578, 0x6972, 0x616E, 0x0000};

static char *put_u16(char *p, uint16_t v)
{
	char t[6];
	uint8_t n = 0;
	do t[n++] = (char)('0' + v % 10); while ((v /= 10) && n < 5);
	while (n)
		*p++ = t[--n];
	return p;
}

void irreceiver_mainloop(void)
{
	if (irreceiver_counter > 0)
		irreceiver_counter--;
	if (!(irreceiver_state & (1u << ST_BUFFER_USED)))
		return;
	irreceiver_error = 0;
	header = address = command = 0;
	irreceiver_count0 = 0;
	irreceiver_count1 = 1;
	read_impuls();
	if (in(irreceiver_pulse, 38, 46)) {
		ir_rc6_analyze();
		if (irreceiver_error == 0) irreceiver_state |= 1u << ST_RC6;
	} else if (in(irreceiver_pulse, 66, 74)) {
		pulse_distance();  /* ir_samsung_analyze */
		if (irreceiver_error == 0) irreceiver_state |= 1u << ST_SAMSUNG;
	} else if (in(irreceiver_pulse, 125, 150)) {
		ir_nec_analyze();
		if (irreceiver_error == 0) irreceiver_state |= 1u << ST_NEC;
	} else {
		irreceiver_error++;
	}
	if (irreceiver_error == 0) {
		uint8_t custom = (registers[BAS_REG_EXT_IRSETUP] >> ST_CUSTOM) & 1;
		if (custom)
			ir_custom_execute();
		if (!custom && (irreceiver_state & (1u << ST_RC6)))
			ir_rc6_execute();
		if (!custom && (irreceiver_state & (1u << ST_NEC)))
			ir_nec_execute();
		if (irreceiver_analyze_echo > 0) {
			char *p = cmd_req_value;
			*p++ = '[';
			p = put_u16(p, header);
			*p++ = ',';
			p = put_u16(p, address);
			*p++ = ',';
			p = put_u16(p, command);
			*p++ = ']';
			*p = 0;
			cmd_load_header(analyze_event);
			cmd_sendevent();
		}
	}
	irreceiver_state &= 0xC0;  /* also releases the buffer */
}

/* --- capture (AVR) --- */
#ifdef __AVR__
#include <avr/interrupt.h>
#include <avr/io.h>

static volatile uint8_t tcc1_state, tcc1_count0, tcc1_count1 = 1;
static uint32_t tcc1_buffer[IR_MAX_BUFFER + 1];

void ir_capture_init(void)
{
	TCC1.PER = 266;              /* config TCC1 = normal, prescale = 8: 32 MHz / 8 / 267 = 15 kHz */
	TCC1.CTRLA = TC_CLKSEL_DIV8_gc;
	PORTC.INT0MASK = 1u << 6;    /* portc_int0mask: PC6 (RS485 master RX, falling edge by PIN6CTRL) */
	PORTC.INTFLAGS = PORT_INT0IF_bm;
	PORTC.INTCTRL = (uint8_t)((PORTC.INTCTRL & ~PORT_INT0LVL_gm) | PORT_INT0LVL_HI_gc);
	irreceiver_state |= 1u << BAS_IRRECEIVER_STATE_ACTIVE;
}

/* iface_ch3_rx_int: the first edge starts the sampling */
ISR(PORTC_INT0_vect)
{
	if (tcc1_state == 0) {
		tcc1_state = 1;  /* running */
		PORTC.INTCTRL &= (uint8_t)~PORT_INT0LVL_gm;
		TCC1.INTCTRLA = TC_OVFINTLVL_HI_gc;
	}
}

/* timer_TCC1_int */
ISR(TCC1_OVF_vect)
{
	uint32_t m = 1UL << tcc1_count0;
	if (PORTC.IN & (1u << 6))
		tcc1_buffer[tcc1_count1] |= m;
	else
		tcc1_buffer[tcc1_count1] &= ~m;
	if (++tcc1_count0 == IR_MAX_COUNT0) {
		tcc1_count0 = 0;
		if (++tcc1_count1 > IR_MAX_BUFFER) {
			TCC1.INTCTRLA = 0;
			if (!(irreceiver_state & (1u << ST_BUFFER_USED))) {
				for (uint8_t i = 1; i <= IR_MAX_BUFFER; i++)
					irreceiver_buffer[i] = tcc1_buffer[i];
				irreceiver_state |= 1u << ST_BUFFER_USED;
			}
			tcc1_state = 0;
			tcc1_count0 = 0;
			tcc1_count1 = 1;
			PORTC.INTFLAGS = PORT_INT0IF_bm;
			PORTC.INTCTRL = (uint8_t)((PORTC.INTCTRL & ~PORT_INT0LVL_gm) | PORT_INT0LVL_HI_gc);
		}
	}
}
#else
void ir_capture_init(void) { irreceiver_state |= 1u << BAS_IRRECEIVER_STATE_ACTIVE; }
#endif
