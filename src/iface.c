/*
 * USART channels for the text protocol (libs/interface/V40/1..4).
 * Defaults as init.bas without stored registers: ch1 38400, ch2 19200, ch3 38400, ch4 38400.
 * PORT: baud rates from the EEPROM registers (reg_baud_ch1..4) and the IR receiver mode
 * of ch3 (PC6 as IR input) follow with M2 (issues #2, #12).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include "iface.h"

#define RXBUF 256  /* BASCOM iface_in_buffer_size = 254 */

struct chan {
	USART_t *u;
	volatile uint8_t buf[RXBUF];
	volatile uint8_t head, tail;
};

/* index = buart channel number of BASCOM (iface_buart_channel) */
static struct chan chans[4] = {
	{ &USARTC0, {0}, 0, 0 },  /* buart 0: ch1 USB-COM */
	{ &USARTC1, {0}, 0, 0 },  /* buart 1: ch3 RS485 master */
	{ &USARTD0, {0}, 0, 0 },  /* buart 2: ch2 audio/mediaport */
	{ &USARTD1, {0}, 0, 0 },  /* buart 3: ch4 RS485 slave */
};
static const uint8_t ch_of_buart[4] = { iface_ch_usb, iface_ch_master, iface_ch_audio, iface_ch_slave };

uint8_t iface_buart_channel[iface_ch_max + 1];
volatile uint8_t iface_todo[iface_ch_max + 1];

/* 32 MHz: BSEL/BSCALE with < 0.2 % error */
static void set_baud(USART_t *u, uint32_t baud)
{
	uint16_t bsel;
	int8_t bscale = 0;
	switch (baud) {
	case 1200:   bsel = 1665; break;
	case 2400:   bsel = 832;  break;
	case 4800:   bsel = 416;  break;
	case 9600:   bsel = 207;  break;
	case 19200:  bsel = 103;  break;
	case 57600:  bsel = 2158; bscale = -6; break;
	case 115200: bsel = 2094; bscale = -7; break;
	default:     bsel = 51;   break;  /* 38400 */
	}
	u->BAUDCTRLA = bsel & 0xFF;
	u->BAUDCTRLB = (uint8_t)((bscale << USART_BSCALE_gp) & USART_BSCALE_gm) | (bsel >> 8);
}

static void open_usart(uint8_t b, uint32_t baud)
{
	USART_t *u = chans[b].u;
	set_baud(u, baud);
	u->CTRLC = USART_CMODE_ASYNCHRONOUS_gc | USART_PMODE_DISABLED_gc | USART_CHSIZE_8BIT_gc;
	u->CTRLA = USART_RXCINTLVL_MED_gc;  /* enable Usartxx_rxc , med */
	u->CTRLB = USART_RXEN_bm | USART_TXEN_bm;
}

void iface_init(void)
{
	iface_buart_channel[iface_ch_usb] = 0;
	iface_buart_channel[iface_ch_master] = 1;
	iface_buart_channel[iface_ch_audio] = 2;
	iface_buart_channel[iface_ch_slave] = 3;
	open_usart(0, 38400);
	open_usart(1, 38400);
	open_usart(2, 19200);
	open_usart(3, 38400);
}

static inline void rx(uint8_t b)
{
	struct chan *c = &chans[b];
	uint8_t d = c->u->DATA;
	uint8_t n = (uint8_t)(c->head + 1);
	if (n == c->tail)
		return;  /* full: drop */
	c->buf[c->head] = d;
	c->head = n;
	if (d == 0x0A)  /* bytematch = 10 -> SerialXcharmatch: incr iface_todo */
		iface_todo[ch_of_buart[b]]++;
}

ISR(USARTC0_RXC_vect) { rx(0); }
ISR(USARTC1_RXC_vect) { rx(1); }
ISR(USARTD0_RXC_vect) { rx(2); }
ISR(USARTD1_RXC_vect) { rx(3); }

uint8_t iface_copy2buffer(uint8_t ch, uint8_t *buffer)
{
	uint8_t b = iface_buart_channel[ch];
	struct chan *c = &chans[b];
	uint8_t result = 0;
	while (c->tail != c->head && result < 127) {  /* PORT: bounded to cmd_buffer(127) */
		uint8_t d = c->buf[c->tail];
		c->tail = (uint8_t)(c->tail + 1);
		buffer[++result] = d;
		if (d == 0x0A)
			break;
	}
	buffer[result + 1] = 0x00;
	uint8_t sreg = SREG;
	cli();
	if (iface_todo[ch])
		iface_todo[ch]--;
	SREG = sreg;
	return result;
}

void iface_putc(uint8_t buart, char ch)
{
	if (buart > 3)
		return;
	USART_t *u = chans[buart].u;
	while (!(u->STATUS & USART_DREIF_bm))
		;
	u->STATUS = USART_TXCIF_bm;
	u->DATA = (uint8_t)ch;
}

void iface_flush(uint8_t buart)
{
	if (buart > 3)
		return;
	USART_t *u = chans[buart].u;
	while (!(u->STATUS & USART_DREIF_bm))
		;
	while (!(u->STATUS & USART_TXCIF_bm))
		;
}
