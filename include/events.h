/*
 * Event fields, port of libs/ctrl/events (functions.bas and the shutdown, usb, grounds,
 * overflow_ad(_da), spdif/ct7302, spdif/cs8416 parts). Indices 1-based as BASCOM.
 * The original updates pins in port interrupts (interrupts.bas); the port polls them in
 * the main loop (events_poll_pins), which gives the same field values for these slow
 * signals. ADC, VU meter and microSD follow.
 */
#ifndef EVENTS_H_
#define EVENTS_H_
#include <stdint.h>
#include "flavour.h"
extern uint8_t events_field_value[BAS_MAX_EVENTS + 1];
extern uint8_t events_is_changed[BAS_MAX_EVENTS + 1];
extern uint32_t events_bind_modc[BAS_MAX_EVENTS + 1];
extern uint8_t modc_is_changed[BAS_MAX_MODC + 1];  /* bit modc_bit_events etc., MODC follows */

void ctrl_events_init(void);
void event_set_value(uint8_t event, uint8_t value);
void event_set_integer(uint8_t event, int16_t value);
uint8_t events_init_get(uint8_t nr);             /* events_field_init_eram(nr) */
void events_init_set(uint8_t nr, uint8_t value);

void events_poll_pins(void);          /* shutdown, usb dtr, grounds, overflow pins */
void events_overflow_mainloop(void);  /* events_overflow_ad(_da)_mainloop */
void events_spdif_init(void);
void events_spdif_mainloop(void);
#endif
