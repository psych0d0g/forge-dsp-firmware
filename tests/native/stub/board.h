/* host replacement of include/board.h: pin changes are printed */
#ifndef BOARD_H_
#define BOARD_H_
#include <stdio.h>
#include "flavour.h"
#define PIN_SET(name) printf("@pin:%s=1\n", #name)
#define PIN_CLR(name) printf("@pin:%s=0\n", #name)
#define PIN_TGL(name) printf("@pin:%s=~\n", #name)
#define PIN_READ(name) 0
static inline void board_init(void) {}
#endif
