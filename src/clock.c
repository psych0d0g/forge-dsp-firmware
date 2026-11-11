/*
 * System clock as main.bas: internal 32 MHz RC, no prescaler, internal 32.768 kHz
 * RC enabled, DFLL for the 32 MHz RC on, 2 MHz RC off.
 */
#include <avr/io.h>
#include <util/delay.h>
#include "clock.h"

static void ccp_write(volatile uint8_t *reg, uint8_t val)
{
	CCP = CCP_IOREG_gc;
	*reg = val;
}

void clock_init(void)
{
	OSC.CTRL |= OSC_RC32MEN_bm | OSC_RC32KEN_bm;
	while (!(OSC.STATUS & OSC_RC32MRDY_bm))
		;
	ccp_write(&CLK.PSCTRL, CLK_PSADIV_1_gc | CLK_PSBCDIV_1_1_gc);
	ccp_write(&CLK.CTRL, CLK_SCLKSEL_RC32M_gc);
	OSC.CTRL &= ~OSC_RC2MEN_bm;                 /* config osc = disabled */
	while (!(OSC.STATUS & OSC_RC32KRDY_bm))     /* bitwait OSC_STATUS.2 */
		;
	OSC.DFLLCTRL |= 0x01;                       /* OSC_DFLLCTRL.0 = 1 (as BASCOM) */
	DFLLRC32M.CTRL |= DFLL_ENABLE_bm;           /* DFLLRC32M_CTRL.0 = 1 */
	_delay_ms(2);
}
