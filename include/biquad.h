/*
 * Biquad coefficients, port of libs/dsp/tas3108/functions.bas (dsp_bq_*) and the type
 * dispatch of cmd_set_bq_calc (libs/cmd/v20/common/subs/cmd_biquad). 64-bit double.
 * Cache indices as BASCOM: values f=1, q=2, gain=3, b=4; coefficients A2=1, A1=2, B2=3,
 * B1=4, B0=5, A0=6 (1-based).
 */
#ifndef BIQUAD_H_
#define BIQUAD_H_
#include <stdint.h>
_Static_assert(sizeof(double) == 8, "build with -mdouble=64 (BASCOM computes biquads in double)");

extern double dsp_bq_cache_values[7];  /* 1-based */
extern uint8_t dsp_bq_cache_type;

/* fill the cache from type, f, gain, q, b, flags as the protocol does, then compute;
 * gain_raw in 0.1 dB, q_raw in 1/1000 (new format) */
void dsp_bq_calc_type(uint8_t type);   /* select case dsp_bq_cache_type (cmd_set_bq_calc) */
void dsp_bq_calcto5coeff(void);
/* coefficients 1..5 -> 5.23 two's complement in 28 bits, buffer_dwords(1..5) */
void dsp_bq_convert2raw(uint32_t raw[6]);
void dsp_bq_default(uint32_t raw[6]);
#endif
