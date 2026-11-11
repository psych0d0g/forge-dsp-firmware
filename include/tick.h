#ifndef TICK_H_
#define TICK_H_
#include <stdint.h>
#define timer_tce0_div_256 0
#define timer_tce0_div_128 1
#define timer_tce0_div_64  2
#define timer_tce0_div_32  3
#define timer_tce0_div_16  4
#define timer_tce0_div_8   5
#define timer_tce0_div_4   6
#define timer_tce0_div_2   7
#define timer_tce0_div_1   8
#define timer_tce0_div_21  9
#define timer_tce0_div_41  10
extern volatile uint16_t timer_tce0_counter;
extern volatile int16_t timer_tce0_value;  /* rotary encoder (MODC volume) */
extern volatile uint8_t timer_tce0_factor, timer_tce0_min, timer_tce0_max;
void tick_init(void);
uint16_t tick_ms(void);
uint16_t loopcall_take(void);  /* pending loopcall bits, cleared */
#endif
