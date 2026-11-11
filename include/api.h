/*
 * Audio API, port of libs/api (functions.bas, mute/, mix/) and the preset load sequence of
 * modc_load_preset (libs/ctrl/modc/v20). Values live in storage_non_biquadvalues (store.h).
 * Mute: the outputs are muted in the DAC; mask = storage_mute_out OR the four priorities
 * (hardware, system, change, modc). "change" unmutes itself 8 main loops after the last
 * change (api_autounmute_counter).
 */
#ifndef API_H_
#define API_H_
#include <stdint.h>
extern uint8_t api_store;
extern int16_t api_autounmute_counter;
extern uint32_t store_mix_changed;  /* bit n: mixer register n of the loaded position changed */
#define api_mute_out_on  0xFF
#define api_mute_out_off 0x00
#define api_mute_out_prio_hardware 4
#define api_mute_out_prio_system   3
#define api_mute_out_prio_change   2
#define api_mute_out_prio_modc     1

void api_init(void);
void api_init_mute(void);
void api_mainloop(void);
void api_set_out_mute(void);
void api_set_out_mute_store(uint8_t v);
void api_set_out_mute_prio(uint8_t prio, uint8_t v);
void api_set_out_mute_change(void);
uint8_t api_get_out_mute_store(void);
void api_set_in_vol(uint8_t ch, uint8_t v);
uint8_t api_get_in_vol(uint8_t ch);
void api_set_spdif_vol(uint8_t ch, uint8_t v);
uint8_t api_get_spdif_vol(uint8_t ch);
void api_set_out_vol(uint8_t ch, uint8_t v);
void api_set_out_vol_hw(uint8_t ch, uint8_t v);
uint8_t api_get_out_vol(uint8_t ch);
void api_set_phase(uint8_t v);
uint8_t api_get_phase(void);
void api_set_delay(uint8_t ch, uint16_t v);
uint16_t api_get_delay(uint8_t ch);
void api_set_delay_in(uint8_t ch, uint16_t v);
uint16_t api_get_delay_in(uint8_t ch);
void api_set_mix_position(uint8_t pos);
void api_set_mix_reg(void);
void api_restore_mix(void);
/* input selection = mux of the active mixer position (issue #23) */
uint8_t api_get_in_input(void);
uint8_t api_set_in_input(uint8_t v);  /* 0 = refused */
void api_restore_in_input(void);
void api_publish_in_input(void);      /* events 45..48 = mux of positions 1..4 */
uint8_t api_in_input_valid(uint8_t v);
uint8_t api_in_input_max(void);
/* modc_load_preset without the MODC parts: load preset and apply everything;
 * remote out (amplifiers) only with FORGE_AUDIO=1 */
void api_preset_load(uint8_t preset);
void api_startup_delay(void);
#endif
