/*
 * TCE0 as in the BASCOM firmware (libs/ctrl/timer): prescale 64, PER 976 -> 512 Hz.
 * timer_TCE0_int: counter, loopcall bits (bit n set when counter bit n rises, i.e. every
 * 2^(n+1) ticks: div_256 = 256 Hz ... div_1 = 1 Hz, div_41 = 1/4 Hz) and the rotary
 * encoder on quad_decoding_2/3. tick_ms is derived (1 tick = 1.954 ms).
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "board.h"
#include "tick.h"

static volatile uint16_t ms;
static uint16_t us_acc;
volatile uint16_t timer_tce0_counter;
volatile uint16_t timer_tce0_loopcall;
static uint16_t timer_tce0_zerocheck;
static uint8_t timer_tce0_rotary;
volatile int16_t timer_tce0_value;
volatile uint8_t timer_tce0_factor = 1, timer_tce0_min = 0, timer_tce0_max = 255;

void tick_init(void)
{
	timer_tce0_zerocheck = timer_tce0_counter;  /* timer_init */
	timer_tce0_loopcall = 0;
	TCE0.PER = 976;
	TCE0.INTCTRLA = TC_OVFINTLVL_LO_gc;
	TCE0.CTRLA = TC_CLKSEL_DIV64_gc;
}

ISR(TCE0_OVF_vect)
{
	us_acc += 1954;  /* 977 * 64 / 32 MHz */
	while (us_acc >= 1000) {
		us_acc -= 1000;
		ms++;
	}
	uint16_t c = ++timer_tce0_counter;
	timer_tce0_loopcall |= (uint16_t)(~timer_tce0_zerocheck & c);
	timer_tce0_zerocheck = c;
#ifdef PIN_QUAD_DECODING_2_PORT
	timer_tce0_rotary = (uint8_t)(timer_tce0_rotary << 2);
	timer_tce0_rotary |= (uint8_t)(PIN_READ(PIN_QUAD_DECODING_2) << 1 | PIN_READ(PIN_QUAD_DECODING_3));
	timer_tce0_rotary &= 0x0F;
	switch (timer_tce0_rotary) {
	case 2: case 13: timer_tce0_value += timer_tce0_factor; break;
	case 7: case 8: timer_tce0_value -= timer_tce0_factor; break;
	}
	if (timer_tce0_value > timer_tce0_max) timer_tce0_value = timer_tce0_max;
	if (timer_tce0_value < timer_tce0_min) timer_tce0_value = timer_tce0_min;
#endif
}

uint16_t tick_ms(void)
{
	uint16_t v;
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { v = ms; }
	return v;
}

uint16_t loopcall_take(void)
{
	uint16_t v;
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		v = timer_tce0_loopcall;
		timer_tce0_loopcall = 0;
	}
	return v;
}
