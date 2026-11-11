/* Port expanders on bus 1: libs/expander (pca9554 @0x40/0x70, @0x4E/0x7E; pca9555 @0x4E/0x7E) */
#ifndef EXPANDER_H_
#define EXPANDER_H_
#include <stdint.h>
extern uint8_t pca9554_bits, pca9554_value;
extern uint16_t pca9555_bits, pca9555_value;
void expander_init(void);          /* the init.bas of every fitted expander, in source order */
void pca9554_4F_set_output(void);  /* bits in pca9554_bits take pca9554_value */
void pca9555_4F_set_output(void);
#endif
