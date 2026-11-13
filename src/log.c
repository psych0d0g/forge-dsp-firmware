/*
 * PORT extension (#60): binary event log, see include/log.h and docs/log-format.md.
 * Events go into a small RAM queue first (the DSP detection runs before the card is mounted) and
 * are appended to FORGE.LOG by log_mainloop. At LOG_MAX_BYTES the file becomes FORGE.OLD (an older
 * FORGE.OLD is deleted) and a new FORGE.LOG starts with a format record. Each write is closed again,
 * so a power cut loses at most the records in the queue.
 * License: CC BY-NC-SA 4.0
 */
#include <string.h>
#include "log.h"
#include "eram.h"
#include "ff.h"
#include "flavour.h"
#include "registers.h"
#include "sd.h"
#include "tick.h"

#define QUEUE 16

static uint8_t q[QUEUE][LOG_RECORD];
static uint8_t q_head, q_n;
static uint16_t dropped;
static uint32_t clock_ms;
static uint16_t clock_last;
static uint8_t boot_counted;  /* sysinit_count_boot ran: the EEPROM holds the number of this start */

void log_boot_counted(void) { boot_counted = 1; }

uint32_t log_now_ms(void)
{
	uint16_t t = tick_ms();
	clock_ms += (uint16_t)(t - clock_last);  /* tick_ms wraps after 65 s: called often enough */
	clock_last = t;
	return clock_ms;
}

uint8_t log_enabled(void)
{
	return (registers[BAS_REG_DEBUGOUT] & (1u << BAS_DEBUGOUT_FILE)) && registers[BAS_REG_DEBUGMODE] > BAS_DBGLVL_OFF;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static void fill(uint8_t *r, uint8_t level, uint8_t module, uint8_t event, uint16_t p1, uint16_t p2)
{
	uint32_t t = log_now_ms();
	put16(r, (uint16_t)(eram_rw(ERAM_DEVICE_BOOTCYCLES_ERAM) + (boot_counted ? 0 : 1)));
	put16(r + 2, (uint16_t)t);
	put16(r + 4, (uint16_t)(t >> 16));
	r[6] = (uint8_t)(level << 5 | (module & 31));
	r[7] = event;
	put16(r + 8, p1);
	put16(r + 10, p2);
}

void log_event(uint8_t level, uint8_t module, uint8_t event, uint16_t p1, uint16_t p2)
{
	if (!log_enabled() || level > registers[BAS_REG_DEBUGMODE])
		return;
	if (q_n == QUEUE) {
		dropped++;
		return;
	}
	fill(q[(q_head + q_n) % QUEUE], level, module, event, p1, p2);
	q_n++;
}

static uint8_t write_rec(FIL *f, const uint8_t *r)
{
	UINT w;
	return f_write(f, r, LOG_RECORD, &w) == FR_OK && w == LOG_RECORD;
}

void log_mainloop(void)
{
	log_now_ms();
	if (!q_n && !dropped)
		return;
	if (!log_enabled()) {
		q_n = 0;  /* switched off meanwhile */
		dropped = 0;
		return;
	}
	static uint32_t last_try;
	static uint8_t tried;
	if (!sd_ready) {
		/* no card or not mountable: try again every 2 s, not in every pass */
		if (tried && clock_ms - last_try < 2000)
			return;
		tried = 1;
		last_try = clock_ms;
	}
	if (!sd_mounted())
		return;  /* kept until a card is there; more than QUEUE are counted as dropped */
	FIL f;
	if (f_open(&f, LOG_FILE, FA_OPEN_APPEND | FA_WRITE) != FR_OK)
		return;
	if (f_size(&f) >= LOG_MAX_BYTES) {
		f_close(&f);
		f_unlink(LOG_OLD);
		if (f_rename(LOG_FILE, LOG_OLD) != FR_OK || f_open(&f, LOG_FILE, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
			return;
	}
	uint8_t r[LOG_RECORD];
	if (f_size(&f) == 0) {
		fill(r, LOG_FATAL, LOGM_LOG, LOGE_LOG_FILE, LOG_FORMAT, LOG_RECORD);
		write_rec(&f, r);
	}
	while (q_n && write_rec(&f, q[q_head])) {
		q_head = (uint8_t)((q_head + 1) % QUEUE);
		q_n--;
	}
	if (dropped && !q_n) {
		fill(r, LOG_WARN, LOGM_LOG, LOGE_LOG_DROPPED, dropped, 0);
		if (write_rec(&f, r))
			dropped = 0;
	}
	f_close(&f);
}
