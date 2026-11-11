#ifndef HWCHECK_H
#define HWCHECK_H
#include <stdint.h>
/* end of systeminit.bas ("checks evaluate"): found peripherals against the variant */
uint16_t hw_check_expected_other(void);
uint16_t hw_check_expected_dsp(void);
uint8_t hw_check_evaluate(void);  /* 1 = mismatch: outputs muted (prio hardware) */
#endif
