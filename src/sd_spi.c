/*
 * SD card in SPI mode (SDSC/SDHC/SDXC) on SPIE and the FatFs disk interface (diskio.h).
 * Port of what MMCSD_HC.LIB/AVR-DOS did in the original (not in the published sources).
 * License: CC BY-NC-SA 4.0.
 */
#ifdef __AVR__
#include <avr/io.h>
#include <util/delay.h>
#include "board.h"
#include "ff.h"
#include "diskio.h"
#include "sd.h"
#include "watchdog.h"

#define CS_LOW()  PIN_CLR(PIN_EXT_MICROSD_SPI_SS)
#define CS_HIGH() PIN_SET(PIN_EXT_MICROSD_SPI_SS)

static uint8_t card_type;  /* 0 none, 1 SDSC (byte address), 2 SDHC/SDXC (block address) */
static DSTATUS stat = STA_NOINIT;

uint8_t sd_card_present(void) { return PIN_READ(PIN_EXT_MICROSD_SW) == 0; }

static uint8_t xfer(uint8_t b)
{
	SPIE.DATA = b;
	while (!(SPIE.STATUS & SPI_IF_bm))
		;
	return SPIE.DATA;
}

static void spi_slow(void) { SPIE.CTRL = SPI_ENABLE_bm | SPI_MASTER_bm | SPI_MODE_0_gc | SPI_PRESCALER_DIV128_gc; }  /* 250 kHz */
static void spi_fast(void) { SPIE.CTRL = SPI_ENABLE_bm | SPI_MASTER_bm | SPI_MODE_0_gc | SPI_PRESCALER_DIV16_gc; }   /* 2 MHz */

static uint8_t wait_ready(uint16_t ms)
{
	for (uint32_t n = (uint32_t)ms * 20; n; n--) {
		if (xfer(0xFF) == 0xFF)
			return 1;
		_delay_us(50);
	}
	return 0;
}

static void deselect(void)
{
	CS_HIGH();
	xfer(0xFF);
}

static uint8_t select(void)
{
	CS_LOW();
	xfer(0xFF);
	if (wait_ready(500))
		return 1;
	deselect();
	return 0;
}

static uint8_t cmd(uint8_t c, uint32_t arg)
{
	if (c & 0x80) {  /* ACMD: CMD55 first */
		c &= 0x7F;
		uint8_t r = cmd(55, 0);
		if (r > 1)
			return r;
	}
	deselect();
	if (!select())
		return 0xFF;
	xfer(0x40 | c);
	xfer((uint8_t)(arg >> 24));
	xfer((uint8_t)(arg >> 16));
	xfer((uint8_t)(arg >> 8));
	xfer((uint8_t)arg);
	xfer(c == 0 ? 0x95 : c == 8 ? 0x87 : 0x01);
	if (c == 12)
		xfer(0xFF);
	uint8_t r, n = 10;
	do
		r = xfer(0xFF);
	while ((r & 0x80) && --n);
	return r;
}

DSTATUS disk_status(BYTE drv) { return drv ? STA_NOINIT : stat; }

DSTATUS disk_initialize(BYTE drv)
{
	if (drv || !sd_card_present())
		return STA_NOINIT | STA_NODISK;
	spi_slow();
	CS_HIGH();
	for (uint8_t i = 0; i < 10; i++)
		xfer(0xFF);  /* >= 74 clocks */
	card_type = 0;
	if (cmd(0, 0) == 1) {  /* idle */
		uint8_t ocr[4];
		uint16_t t = 1000;  /* ms */
		if (cmd(8, 0x1AA) == 1) {  /* SD v2 */
			for (uint8_t i = 0; i < 4; i++)
				ocr[i] = xfer(0xFF);
			if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
				while (t-- && cmd(0x80 | 41, 1UL << 30))
					_delay_ms(1), watchdog_reset();
				if (t && cmd(58, 0) == 0) {
					for (uint8_t i = 0; i < 4; i++)
						ocr[i] = xfer(0xFF);
					card_type = (ocr[0] & 0x40) ? 2 : 1;
				}
			}
		} else {  /* SD v1 */
			while (t-- && cmd(0x80 | 41, 0))
				_delay_ms(1), watchdog_reset();
			if (t && cmd(16, 512) == 0)
				card_type = 1;
		}
	}
	deselect();
	if (card_type) {
		spi_fast();
		stat &= (DSTATUS)~STA_NOINIT;
	} else {
		stat = STA_NOINIT;
	}
	return stat;
}

static uint8_t rcvr_block(BYTE *buf, uint16_t n)
{
	uint8_t token;
	uint16_t t = 2000;
	do {
		token = xfer(0xFF);
		if (token == 0xFF)
			_delay_us(100);
	} while (token == 0xFF && --t);
	if (token != 0xFE)
		return 0;
	while (n--)
		*buf++ = xfer(0xFF);
	xfer(0xFF);  /* CRC */
	xfer(0xFF);
	return 1;
}

static uint8_t xmit_block(const BYTE *buf, uint8_t token)
{
	if (!wait_ready(500))
		return 0;
	xfer(token);
	for (uint16_t i = 0; i < 512; i++)
		xfer(buf[i]);
	xfer(0xFF);
	xfer(0xFF);
	return (xfer(0xFF) & 0x1F) == 0x05;
}

DRESULT disk_read(BYTE drv, BYTE *buf, LBA_t sector, UINT count)
{
	if (drv || !count)
		return RES_PARERR;
	if (stat & STA_NOINIT)
		return RES_NOTRDY;
	if (card_type == 1)
		sector *= 512;
	while (count) {
		if (cmd(17, sector) != 0 || !rcvr_block(buf, 512))
			break;
		buf += 512;
		sector += card_type == 1 ? 512 : 1;
		count--;
		watchdog_reset();
	}
	deselect();
	return count ? RES_ERROR : RES_OK;
}

DRESULT disk_write(BYTE drv, const BYTE *buf, LBA_t sector, UINT count)
{
	if (drv || !count)
		return RES_PARERR;
	if (stat & STA_NOINIT)
		return RES_NOTRDY;
	if (card_type == 1)
		sector *= 512;
	while (count) {
		if (cmd(24, sector) != 0 || !xmit_block(buf, 0xFE))
			break;
		buf += 512;
		sector += card_type == 1 ? 512 : 1;
		count--;
		watchdog_reset();
	}
	deselect();
	return count ? RES_ERROR : RES_OK;
}

DRESULT disk_ioctl(BYTE drv, BYTE ctrl, void *buf)
{
	(void)buf;
	if (drv)
		return RES_PARERR;
	if (stat & STA_NOINIT)
		return RES_NOTRDY;
	if (ctrl == CTRL_SYNC) {
		uint8_t ok = select();
		deselect();
		return ok ? RES_OK : RES_ERROR;
	}
	return RES_PARERR;
}
#endif
