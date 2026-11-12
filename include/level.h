#ifndef LEVEL_H_
#define LEVEL_H_
#include <stdint.h>
/*
 * Input level measurement (port extension, issue #51): RMS level of selected inputs of DSP
 * core 1 for the loudness block (dynamic equal-loudness compensation).
 *  - level_mainloop (64 Hz) reads one input sample per call with TAS3108 PEEK (data memory
 *    D_000..D_007 = in1..in8, written by ST(DI,DATA,n) every sample), round robin over the
 *    selected inputs. The reads are asynchronous to the audio clock, so they are random
 *    samples of the signal; their mean square is the signal power.
 *  - Exponential average of the power with time constant tau; a fast average (0.1 s) gates:
 *    below LEVEL_GATE_DB the slow average holds, so pauses do not raise the loudness boost.
 *  - level_value(): the level in the scale of gain.* (255 = 0 dBFS RMS, 0.5 dB per step).
 */
#define LEVEL_RATE_HZ 64
#define LEVEL_GATE_DB (-60)
#define LEVEL_GATE_POWER 1e-6   /* 10^(LEVEL_GATE_DB/10) */
#define LEVEL_FAST_S 0.1   /* gate detector: a sudden stop costs the slow average < 1 dB */

void level_init(void);
void level_mainloop(void);                 /* one PEEK per call, LEVEL_RATE_HZ */
void level_config(uint8_t mask, uint8_t tau_tenths);  /* mask bit n = in(n+1); 0 = off */
void level_feed(double sample);            /* one sample, full scale 1.0 (tests, mainloop) */
uint8_t level_valid(void);                 /* 1 once a level above the gate was seen */
uint8_t level_value(void);                 /* gain scale, 255 = 0 dBFS RMS */
#endif
