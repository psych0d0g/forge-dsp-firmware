/* S/PDIF receivers / SRC CT7302 #20 (0x20) and #22 (0x22), libs/spdif/ct7302 */
#ifndef CT7302_H_
#define CT7302_H_
#include <stdint.h>
#include "flavour.h"
#if BAS_MODUL_CT7302 > 0
extern uint8_t ct7302_active_channel;
void spdif_init(void);                       /* ct7302/20 and /22 init.bas */
void ct7302_20_set_mux(uint8_t channel);     /* input; with usb_at_i2s: 1 -> RX5 */
void ct7302_20_set_vol(uint8_t channel, uint8_t value);  /* register = 255 - value */
uint8_t ct7302_20_get_mux(void);
void ct7302_22_set_mux(uint8_t channel);
uint8_t ct7302_20_get_form_state(void);
uint8_t ct7302_22_get_form_state(void);
#else
void spdif_init(void);                       /* SRC43x2 + CS8416 (V30-96LN) */
void src43x2_set_mux(uint8_t channel);
void src43x2_set_vol(uint8_t channel, uint8_t value);
uint8_t cs8416_get_rec_error(void);
uint8_t cs8416_get_form_state(void);
void cs8416_set_mux(uint8_t v);
#endif
#endif
