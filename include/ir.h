/*
 * IR receiver, port of libs/extender/ir-receiver (NEC, RC6, Samsung, custom codes).
 * With registers(reg_ext_irsetup).irreceiver_state_on the RS485 master RX pin (PC6) is the
 * IR input: a falling edge starts TCC1 (~15 kHz), 40 x 32 samples are taken, then decoded
 * in irreceiver_mainloop (1/256 s).
 */
#ifndef IR_H_
#define IR_H_
#include <stdint.h>
#include "flavour.h"
#define IR_MAX_BUFFER 40
#define IR_MAX_COUNT0 32
#define IR_CUSTOM_MAX 18
extern volatile uint8_t irreceiver_state;
extern volatile uint32_t irreceiver_buffer[IR_MAX_BUFFER + 1];  /* 1-based, bit = pin level */
extern uint16_t irreceiver_frames[4];                           /* 1 header, 2 address, 3 command */
extern int16_t irreceiver_value;
extern uint8_t irreceiver_value_min, irreceiver_value_max, irreceiver_factor;
extern uint8_t irreceiver_analyze_echo;
extern uint16_t irreceiver_custom[4][IR_CUSTOM_MAX + 1];         /* [1..3][1..18] */

void irreceiver_init(void);
void ir_custom_init(void);
void ir_custom_set(uint8_t i, uint16_t header, uint16_t address, uint16_t command);
void irreceiver_mainloop(void);
void ir_capture_init(void);  /* AVR: PORTC INT0 on PC6 + TCC1, only with the IR master on */
#endif
