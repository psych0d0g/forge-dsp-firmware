#ifndef POWER_H_
#define POWER_H_
#include <stdint.h>
void power_hold(void);
void power_poll(uint16_t ms_now);
void mcu_reset(void) __attribute__((noreturn));
#endif
