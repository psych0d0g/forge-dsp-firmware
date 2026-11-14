/*
 * USART channels for the text protocol (libs/interface/V40/1..4).
 * Baud rates from registers(reg_baud_ch1..4) as libs/interface/V40/<n>/init.bas, defaults
 * ch1 38400, ch2 19200, ch3 38400, ch4 38400. Debug output on ch2/ch4 forces 115200.
 * ch3 is not opened as UART while the IR receiver uses PC6 (reg_ext_irsetup).
 * The IR receiver (src/ir.c) samples PC6 then.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include "iface.h"
#include "registers.h"

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

/* register code (uart_baud_*) -> rate; anything else -> the channel default */
static uint32_t baud_of(uint8_t code, uint32_t dflt)
{
	switch (code) {
	case 1:  return 1200;
	case 2:  return 2400;
	case 4:  return 4800;
	case 8:  return 9600;
	case 16: return 19200;
	case 32: return 38400;
	case 48: return 57600;
	case 96: return 115200;
	default: return dflt;
	}
}

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
	case 230400: bsel = 983;  bscale = -7; break;  /* PORT (#62): 0.01 % */
	case 460800: bsel = 428;  bscale = -7; break;  /* PORT (#62): 0.08 % */
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
	open_usart(0, baud_of(registers[BAS_REG_BAUD_CH1], 38400));
	if (registers[BAS_REG_DEBUGOUT] & (1 << BAS_DEBUGOUT_UART2))
		set_register(BAS_REG_BAUD_CH2, BAS_UART_BAUD_115200);
	open_usart(2, baud_of(registers[BAS_REG_BAUD_CH2], 19200));
	uint8_t ir = registers[BAS_REG_EXT_IRSETUP];
	if (!(ir & (1 << BAS_IRRECEIVER_STATE_ON)) || (ir & (1 << BAS_IRRECEIVER_STATE_NOTINIT)))
		open_usart(1, baud_of(registers[BAS_REG_BAUD_CH3], 38400));
	if (registers[BAS_REG_DEBUGOUT] & (1 << BAS_DEBUGOUT_UART4))
		set_register(BAS_REG_BAUD_CH4, BAS_UART_BAUD_115200);
	open_usart(3, baud_of(registers[BAS_REG_BAUD_CH4], 38400));
}

static inline void rx(uint8_t b)
{
	struct chan *c = &chans[b];
	uint8_t d = c->u->DATA;
	uint8_t n = (uint8_t)(c->head + 1);
	if (n == c->tail) {
		/* PORT (#66): full. Without a line end in it (noise, a wrong rate) it would stay full and the
		 * channel deaf until a restart (the original drops the new byte, also every line end); the
		 * buffer is emptied instead, a following line arrives again. */
		if (iface_todo[ch_of_buart[b]] == 0) {
			c->tail = c->head;
			return;
		}
		return;  /* full of lines not yet read: drop */
	}
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
	if (!(u->CTRLB & USART_TXEN_bm))
		return;  /* not opened (e.g. reset during startup): TXCIF would never be set */
	while (!(u->STATUS & USART_DREIF_bm))
		;
	while (!(u->STATUS & USART_TXCIF_bm))
		;
}

/* PORT extension (#62): the USB-COM faster on request, for the GUI. Not stored: after a restart and
 * after USB_IDLE_MS without a line on it, it falls back to 38400, so the original GUI and the
 * bootloader tools always find it. */
#include "tick.h"
#define USB_IDLE_MS 30000u
static uint8_t usb_fast;
static uint16_t usb_last;
void iface_usb_baud(uint32_t baud)
{
	iface_flush(0);  /* the answer leaves at the old rate */
	set_baud(chans[0].u, baud);
	usb_fast = baud != 38400;
	usb_last = tick_ms();
}
void iface_usb_line(void) { usb_last = tick_ms(); }
void iface_usb_poll(void)
{
	if (usb_fast && (uint16_t)(tick_ms() - usb_last) > USB_IDLE_MS) {
		set_baud(chans[0].u, 38400);
		usb_fast = 0;
	}
}
