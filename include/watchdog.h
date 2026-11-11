#ifndef WATCHDOG_H_
#define WATCHDOG_H_
#include <avr/io.h>
/* XMEGA WDT, period 8 s (BASCOM: config watchdog = 8000) */
static inline void watchdog_enable_8s(void)
{
	CCP = CCP_IOREG_gc;
	WDT.CTRL = WDT_PER_8KCLK_gc | WDT_ENABLE_bm | WDT_CEN_bm;
	while (WDT.STATUS & WDT_SYNCBUSY_bm)
		;
}
static inline void watchdog_disable(void)
{
	CCP = CCP_IOREG_gc;
	WDT.CTRL = WDT_CEN_bm;
	while (WDT.STATUS & WDT_SYNCBUSY_bm)
		;
}
static inline void watchdog_reset(void) { __asm__ __volatile__("wdr"); }
#endif
