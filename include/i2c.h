/*
 * I2C, port of libs/interface/V40/5 (bus 1, TWIC, BASCOM channel #5) and /6 (bus 2, TWIE, #6).
 * Same calling convention as BASCOM: the globals i2c_device_addr, i2c_addr / i2c_reg(_h/_l),
 * i2c_size and buffer_bytes are set before a call. Buffers go out and come in
 * BACKWARDS: buffer_bytes(i2c_size) first, buffer_bytes(1) last.
 */
#ifndef I2C_H_
#define I2C_H_
#include <stdint.h>
#include "flavour.h"

extern uint8_t buffer_bytes[BAS_BUFFER_BYTES_MAX + 1];  /* 1-based, libs/misc/buffer_bytes */
#define buffer_byte (buffer_bytes[1])                   /* overlay at buffer_bytes(1) */
static inline uint16_t buffer_word_get(void) { return buffer_bytes[1] | (uint16_t)buffer_bytes[2] << 8; }
static inline void buffer_word_set(uint16_t v) { buffer_bytes[1] = v & 0xFF; buffer_bytes[2] = v >> 8; }

extern uint8_t i2c_device_addr;
extern uint16_t i2c_addr;                                /* i2c_regs(1..2) */
#define i2c_reg_l (*((uint8_t *)&i2c_addr))             /* little endian: regs(1) */
#define i2c_reg_h (*((uint8_t *)&i2c_addr + 1))         /* regs(2) */
#define i2c_reg   i2c_reg_l
extern uint8_t i2c_size;
uint8_t i2c_ee_read(uint8_t bus, uint8_t dev, uint16_t addr, uint8_t *dst, uint8_t len);  /* PORT (#66) */
void i2c_speed(uint8_t bus, uint8_t fast);  /* PORT (#66): FORGE_I2C_FAST_KHZ for checked transfers, else the base clock */
#define I2C_CHECK 2  /* PORT (#67): i2c_speed(bus, I2C_CHECK) = FORGE_I2C_CHECK_KHZ, only for reads checked against a sum */
extern uint16_t channel_open;                            /* bit 5 = bus 1, bit 6 = bus 2 */
extern uint8_t i2c_err;                                  /* BASCOM "err" of the last transfer */

void i2c_bus_init(uint8_t bus);                          /* open "twic"/"twie", 100 kHz */
void i2c_reset(uint8_t bus);
void i2c_wbyte(uint8_t bus);
void i2c_wbuffer(uint8_t bus);
void i2c_wbyte_highreg(uint8_t bus);
void i2c_wbuffer_highreg(uint8_t bus);
void i2c_wbyte_eeprom(uint8_t bus);
void i2c_wbuffer_eeprom(uint8_t bus);
void i2c_rbyte(uint8_t bus);
void i2c_rbuffer(uint8_t bus);
void i2c_rbyte_highreg(uint8_t bus);
void i2c_rbuffer_highreg(uint8_t bus);
/* "read data_size" (word), then data_size/3 triples reg, value, mask:
 * mask 0 = skip, 0xFF = write, else read-modify-write. r points at "restore <label>" */
struct bas_reader_s;
void i2c_wbyte_sequence(uint8_t bus, const uint8_t *table);

/* low level, as BASCOM i2cstart / i2cwbyte / i2crepstart / i2crbyte / i2cstop;
 * provided by i2c_twi.c (AVR) or the host test harness */
void twi_start(uint8_t bus);
void twi_repstart(uint8_t bus);
void twi_wbyte(uint8_t bus, uint8_t b);
uint8_t twi_rbyte(uint8_t bus, uint8_t ack);
void twi_stop(uint8_t bus);
uint8_t twi_sda(uint8_t bus);                            /* i2c1_sda / i2c2_sda pin level */
void twi_bus_clear(uint8_t bus);
uint8_t twi_scl_out(uint8_t bus);                        /* i2cN_scl = PORTx.OUT bit 1 */
uint8_t twi_master_status(uint8_t bus);                  /* TWIx_MASTER_STATUS */                         /* the 8 SCL pulses of i2cN_reset */
void delay_ms(uint16_t ms);
void i2c_sda_wait4silence(uint8_t bus, uint8_t pre, uint8_t s);
/* PORT diagnostics (get system.hardware.dsp): the last wait for the TAS3108 program load per bus */
struct dspboot_diag {
	uint16_t sda_low_ms;  /* ms after `pre` until SDA went low (load started) */
	uint16_t edges;       /* SDA edges counted while waiting */
	uint16_t quiet_ms;    /* ms waited for silence */
	uint8_t timeout;      /* 1: SDA did not go low within 2 s (the original waits for the watchdog) */
	uint8_t retries;      /* PORT (#45): extra probes until the cores on this bus answered */
};
extern struct dspboot_diag dspboot_diag[3];
#endif
