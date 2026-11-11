/*
 * USARTC0 = USB-COM (CP2102, X105), 38400 8N1 as rtx.bas (Com1).
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include "uart.h"

#define RXBUF 128  /* power of two */
static volatile uint8_t rx_buf[RXBUF];
static volatile uint8_t rx_head, rx_tail;

void uart_init(void)
{
	/* 32 MHz, 38400: BSEL 51, BSCALE 0 -> 38462 Bd (+0.16 %) */
	USARTC0.BAUDCTRLA = 51;
	USARTC0.BAUDCTRLB = 0;
	USARTC0.CTRLC = USART_CMODE_ASYNCHRONOUS_gc | USART_PMODE_DISABLED_gc | USART_CHSIZE_8BIT_gc;
	USARTC0.CTRLA = USART_RXCINTLVL_LO_gc;
	USARTC0.CTRLB = USART_RXEN_bm | USART_TXEN_bm;
}

ISR(USARTC0_RXC_vect)
{
	uint8_t c = USARTC0.DATA;
	uint8_t n = (rx_head + 1) & (RXBUF - 1);
	if (n != rx_tail) {
		rx_buf[rx_head] = c;
		rx_head = n;
	}
}

int uart_getc(void)
{
	if (rx_head == rx_tail)
		return -1;
	uint8_t c = rx_buf[rx_tail];
	rx_tail = (rx_tail + 1) & (RXBUF - 1);
	return c;
}

void uart_putc(char c)
{
	while (!(USARTC0.STATUS & USART_DREIF_bm))
		;
	USARTC0.STATUS = USART_TXCIF_bm;  /* cleared here, set when this byte is out */
	USARTC0.DATA = c;
}

void uart_puts(const char *s)
{
	while (*s)
		uart_putc(*s++);
}

void uart_flush(void)
{
	/* wait until the last byte has left the shift register */
	while (!(USARTC0.STATUS & USART_DREIF_bm))
		;
	while (!(USARTC0.STATUS & USART_TXCIF_bm))
		;
}
