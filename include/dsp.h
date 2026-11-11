/*
 * DSP register access, port of libs/dsp/tas3108/functions.bas (dsp_set_*, dsp_get_*) and
 * cores/{1,2,3}/functions.bas. Globals as BASCOM: dsp_core (1..3), dsp_reg (sub address),
 * data in buffer_bytes (buffer_dword = buffer_bytes(1..4), little endian, sent backwards).
 */
#ifndef DSP_H_
#define DSP_H_
#include <stdint.h>
extern uint8_t dsp_core, dsp_reg;
extern uint16_t dsp_bq_virt_nr;
extern uint16_t iface_verify_error_count;

void dsp_calc_core(void);           /* dsp_core = lookup(dsp_bq_virt_nr, data_flow_dsp_bq_core) */
void dsp_calc_reg(void);            /* dsp_reg  = lookup(dsp_bq_virt_nr, data_flow_dsp_bq_reg) */
void dsp_set_biquad(const uint32_t raw[6]);  /* raw[1..5] -> buffer_dwords(1..5) */
void dsp_get_biquad(uint32_t raw[6]);
void dsp_set_vol_out(int32_t vol_calc);      /* buffer_long = dsp_vol_calc, verified write */
void dsp_set_mux(uint32_t state);
uint32_t dsp_get_mux(void);
void dsp_set_delay(uint32_t value);
void dsp_ack_delay(void);
void dsp_set_phase_reg(uint32_t value);      /* one output register, dsp_core/dsp_reg set */
void dsp_init_vum(void);
void dsp_get_vum(void);
#endif
