/* host replacement of include/board.h: pin changes are printed */
#ifndef BOARD_H_
#define BOARD_H_
#include <stdio.h>
#include <stdint.h>
#include "flavour.h"
#define PIN_SET(name) printf("@pin:%s=1\n", #name)
#define PIN_CLR(name) printf("@pin:%s=0\n", #name)
#define PIN_TGL(name) printf("@pin:%s=~\n", #name)
int host_pin_read(const char *name);  /* "#pin=NAME,v" in the harness, default 0 */
#define PIN_READ(name) ((uint8_t)host_pin_read(#name))
static inline void board_init(void) {}
#endif
