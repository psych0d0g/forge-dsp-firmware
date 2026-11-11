/* 1 ms tick on TCE0 (the BASCOM firmware uses TCE0 as its timer as well). */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "tick.h"

static volatile uint16_t ms;

void tick_init(void)
{
	TCE0.PER = (F_CPU / 64 / 1000) - 1;  /* 499 */
	TCE0.INTCTRLA = TC_OVFINTLVL_LO_gc;
	TCE0.CTRLA = TC_CLKSEL_DIV64_gc;
}

ISR(TCE0_OVF_vect)
{
	ms++;
}

uint16_t tick_ms(void)
{
	uint16_t v;
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { v = ms; }
	return v;
}
