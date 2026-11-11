/*
 * Serial channels, port of libs/interface (V40): 4 USARTs with line buffers.
 *   ch1 USB-COM (USARTC0, buart 0), ch2 audio/mediaport (USARTD0, buart 2),
 *   ch3 RS485 master (USARTC1, buart 1), ch4 RS485 slave (USARTD1, buart 3).
 * A line is complete at LF (BASCOM: config serialin ... bytematch = 10).
 */
#ifndef IFACE_H_
#define IFACE_H_
#include <stdint.h>

#define iface_ch_1 1
#define iface_ch_2 2
#define iface_ch_3 3
#define iface_ch_4 4
#define iface_ch_max iface_ch_4
#define iface_ch_usb    iface_ch_1
#define iface_ch_audio  iface_ch_2
#define iface_ch_master iface_ch_3
#define iface_ch_slave  iface_ch_4

#define iface_in_buffer_size 254

extern uint8_t iface_buart_channel[iface_ch_max + 1];   /* 1-based */
extern volatile uint8_t iface_todo[iface_ch_max + 1];   /* complete lines waiting */

void iface_init(void);
/* copy one line (up to and including LF) to buffer[1..], buffer[result+1] = 0 */
uint8_t iface_copy2buffer(uint8_t ch, uint8_t *buffer);
void iface_putc(uint8_t buart, char c);
void iface_flush(uint8_t buart);

/* hooks used by the router, provided by the board/event code */
uint8_t iface_hook_ir_master_online(void);  /* registers(reg_ext_irsetup).irreceiver_state_on */
uint8_t iface_hook_usb_dtr(void);           /* events_field_value(events_field_ext_usb_dtr) */
void iface_hook_busy(uint8_t on);           /* led_yellow while a line is processed */
void iface_hook_watchdog(void);
#endif
