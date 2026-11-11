/*
 * Board access common to all flavours. Pin numbers come from the generated
 * src/flavours/<flavour>/flavour.h (pins.bas of the BASCOM firmware).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#ifndef BOARD_H_
#define BOARD_H_

#include <avr/io.h>
#include "flavour.h"

#define PIN_SET(name)  ((name##_PORT).OUTSET = name##_bm)
#define PIN_CLR(name)  ((name##_PORT).OUTCLR = name##_bm)
#define PIN_TGL(name)  ((name##_PORT).OUTTGL = name##_bm)
/* value of the IN register (includes PINnCTRL inversion, as BASCOM PINx.n) */
#define PIN_READ(name) (((name##_PORT).IN >> name##_bp) & 1)

static inline void board_init(void) { flavour_pins_init(); }

#endif
