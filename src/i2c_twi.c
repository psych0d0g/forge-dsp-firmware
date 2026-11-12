/*
 * XMEGA TWI master as BASCOM's i2cstart / i2cwbyte / i2crepstart / i2crbyte / i2cstop
 * (config twic/twie = 100000). Bus 1 = TWIC (SDA PC0, SCL PC1), bus 2 = TWIE (PE0, PE1).
 * PORT: transfers time out after ~10 ms (BASCOM waits); the error is kept in i2c_err.
 * License: CC BY-NC-SA 4.0
 */
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "board.h"
#include "i2c.h"
#include "watchdog.h"

#define TWI_BAUDVAL ((F_CPU / (2UL * 100000UL)) - 5)  /* 155 */
#define TIMEOUT 40000u

uint8_t i2c_err;
static uint8_t pending_start[3], reading[3];

static TWI_t *twi(uint8_t bus) { return bus == 1 ? &TWIC : &TWIE; }
static PORT_t *port(uint8_t bus) { return bus == 1 ? &PORTC : &PORTE; }

void delay_ms(uint16_t ms)
{
	while (ms--) {
		_delay_ms(1);
		watchdog_reset();
	}
}

void i2c_bus_init(uint8_t bus)
{
	PORT_t *p = port(bus);
	p->OUTSET = PIN0_bm | PIN1_bm;  /* "set both sda & scl to high to reset the I2C interface" */
	TWI_t *t = twi(bus);
	t->MASTER.BAUD = TWI_BAUDVAL;
	t->MASTER.CTRLA = TWI_MASTER_ENABLE_bm;
	t->MASTER.STATUS = TWI_MASTER_BUSSTATE_IDLE_gc;
	channel_open |= 1u << (4 + bus);
	i2c_reset(bus);
}

static uint8_t wait(TWI_t *t, uint8_t flags)
{
	for (uint16_t n = 0; n < TIMEOUT; n++)
		if (t->MASTER.STATUS & flags)
			return 1;
	i2c_err = 1;
	t->MASTER.STATUS = TWI_MASTER_BUSSTATE_IDLE_gc;
	return 0;
}

void twi_start(uint8_t bus) { pending_start[bus] = 1; i2c_err = 0; }
void twi_repstart(uint8_t bus) { pending_start[bus] = 1; }

void twi_wbyte(uint8_t bus, uint8_t b)
{
	TWI_t *t = twi(bus);
	if (pending_start[bus]) {
		pending_start[bus] = 0;
		reading[bus] = b & 1;
		t->MASTER.ADDR = b;  /* (repeated) start + address */
		if (reading[bus]) {
			/* the first byte is clocked in automatically; NACK on the address sets WIF */
			if (!wait(t, TWI_MASTER_RIF_bm | TWI_MASTER_WIF_bm) || (t->MASTER.STATUS & TWI_MASTER_WIF_bm))
				i2c_err = 1;
			return;
		}
	} else {
		t->MASTER.DATA = b;
	}
	if (wait(t, TWI_MASTER_WIF_bm) && (t->MASTER.STATUS & TWI_MASTER_RXACK_bm))
		i2c_err = 1;  /* NACK */
}

uint8_t twi_rbyte(uint8_t bus, uint8_t ack)
{
	TWI_t *t = twi(bus);
	if (!wait(t, TWI_MASTER_RIF_bm))
		return 0xFF;
	uint8_t v = t->MASTER.DATA;
	if (ack)
		t->MASTER.CTRLC = TWI_MASTER_CMD_RECVTRANS_gc;  /* ACK, next byte */
	return v;  /* NACK + STOP follow in twi_stop */
}

void twi_stop(uint8_t bus)
{
	TWI_t *t = twi(bus);
	t->MASTER.CTRLC = (reading[bus] ? TWI_MASTER_ACKACT_bm : 0) | TWI_MASTER_CMD_STOP_gc;
	reading[bus] = 0;
}

uint8_t twi_sda(uint8_t bus) { return port(bus)->IN & PIN0_bm ? 1 : 0; }
uint8_t twi_scl_out(uint8_t bus) { return port(bus)->OUT & PIN1_bm ? 1 : 0; }
uint8_t twi_master_status(uint8_t bus) { return twi(bus)->MASTER.STATUS; }

void twi_bus_clear(uint8_t bus)
{
	TWI_t *t = twi(bus);
	PORT_t *p = port(bus);
	t->MASTER.CTRLA &= ~TWI_MASTER_ENABLE_bm;  /* TWIx_MASTER_CTRLA.3 = 0 */
	for (uint8_t i = 0; i < 8; i++) {
		p->OUTCLR = PIN1_bm;
		_delay_us(1);
		p->OUTSET = PIN1_bm;
		_delay_us(1);
	}
	p->OUTSET = PIN0_bm;                       /* portc.0 = 1 */
	t->MASTER.CTRLA |= TWI_MASTER_ENABLE_bm;
	t->MASTER.STATUS = TWI_MASTER_BUSSTATE_IDLE_gc;
}

/* libs/interface/V40/5,6: i2cN_sda_wait4silence. The TAS3108 load their program from the
 * EEPROM as bus masters after the reset is released; wait until SDA was quiet for s ms.
 * PORT: "bitwait sda, reset" (DSP starts loading) gets a 2 s limit instead of waiting
 * for the watchdog. */
static volatile uint16_t sda_count[3];
ISR(PORTC_INT1_vect) { sda_count[1]++; }
ISR(PORTE_INT1_vect) { sda_count[2]++; }

struct dspboot_diag dspboot_diag[3];

void i2c_sda_wait4silence(uint8_t bus, uint8_t pre, uint8_t s)
{
	PORT_t *p = port(bus);
	struct dspboot_diag *d = &dspboot_diag[bus];
	p->INT1MASK = PIN0_bm;                                   /* portX_int1mask = 1 */
	p->INTCTRL = (p->INTCTRL & ~PORT_INT1LVL_gm) | PORT_INT1LVL_LO_gc;
	uint16_t c0 = sda_count[bus];
	uint16_t w = (uint16_t)(c0 - 1);
	delay_ms(pre);
	uint16_t t = 0;
	for (; (p->IN & PIN0_bm) && t < 2000; t++)
		delay_ms(1);
	d->sda_low_ms = t;
	d->timeout = t >= 2000;
	uint16_t q = 0;
	while (w != sda_count[bus]) {
		w = sda_count[bus];
		delay_ms(s);
		q += s;
	}
	d->quiet_ms = q;
	d->edges = (uint16_t)(sda_count[bus] - c0);
	p->INTCTRL &= ~PORT_INT1LVL_gm;                          /* disable PORTx_INT1 */
}
