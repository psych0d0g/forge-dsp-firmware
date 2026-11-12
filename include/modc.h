/*
 * MODC ("modular control"), port of libs/ctrl/modc/v20: 32 control blocks, each with a
 * type, 4 flags, 4 input events and 4 sets (EEPROM), output = event field 96+n.
 * Arrays are 1-based as BASCOM; flags/events/sets are 4 x 32 (index (k-1)*32 + n).
 */
#ifndef MODC_H_
#define MODC_H_
#include <stdint.h>
#include "flavour.h"
#define MODC_N BAS_MAX_MODC
extern uint8_t modc_enable[MODC_N + 1], modc_type[MODC_N + 1];
extern uint8_t modc_flags[4 * MODC_N + 1], modc_events[4 * MODC_N + 1], modc_sets[4 * MODC_N + 1];
extern uint16_t modc_cfg_store[MODC_N + 1];
extern uint16_t modc_active_affect;
extern uint8_t modc_current_moc;
extern uint8_t modc_loadpreset, modc_cleanup;

void ctrl_modc_init(void);
void ctrl_modc_mainloop(void);
void modc_chk_affect(void);
void modc_set_run(uint8_t typ);
void modc_request_preset(uint8_t preset);
/* cmd_modc: modc_current_moc is set by the caller, nr = 1..4 */
void modc_set_enable(uint8_t value);
void modc_set_type(uint8_t value);
void modc_set_flags(uint8_t nr, uint8_t value);
void modc_set_event(uint8_t nr, uint8_t value);
void modc_set_set(uint8_t nr, uint8_t value);
uint8_t modc_shutdown_enabled(void);
/* port extension (M6, #6): MODC type 0x6C loudness, declared in the original (modc_type_loudness) */
#define MODC_TYPE_LOUDNESS 0x6C
/* port extension (#17): MODC type 0x60 mux_I (input switch), declared in the original */
#define MODC_TYPE_MUX_I 0x60
/* port extension (#14): MODC type 0x6A mono per output pair, declared in the original */
#define MODC_TYPE_MONO 0x6A
/* port extension (#16): MODC type 0x6D mixer (analog input gain), declared in the original */
#define MODC_TYPE_MIXER 0x6D
uint8_t modc_type_version(uint8_t type6);  /* data_mods_versions plus the port's types */
void modc_loudness_invalidate(void);       /* biquads were overwritten (preset load) */
void modc_loudness_touch(void);            /* volume source changed (gain.spdif) */  /* an enabled shutdown block exists */

/* hook: remote out off, peripherals into reset, release the supply, reset (power.c) */
void power_shutdown(uint8_t wait_before_remote_off, uint8_t wait_before_power_off);
#endif
