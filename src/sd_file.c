/*
 * PORT extension (#59): files on the microSD over the command line, for the GUI (download,
 * upload, delete) and later the binary log. One file is open at a time.
 *   set system.sd.open="NAME.EXT"    open for reading (block 0)
 *   set system.sd.create="NAME.EXT"  create or truncate for writing
 *   get system.sd.read               next block: [block, "hex", crc16], at most SD_BLOCK bytes,
 *                                    shorter at the end, [block, "", 0] after it
 *   set system.sd.seek=B             to block B (repeat a lost block); reading only
 *   set system.sd.write="hex:CCCC"   append up to 48 bytes; CCCC = crc16 of them (4 hex digits).
 *                                    Mandatory: the command buffer holds 127 characters and cuts a
 *                                    longer line, line checksum included, without an error; a cut
 *                                    block fails the crc and nothing is written. The GUI sends 40.
 *   set system.sd.close=1            close (writes the buffers to the card)
 *   set system.sd.delete="NAME.EXT"  delete a file (not the open one)
 * Names: 8.3 in the root directory, no paths. crc16 = CRC-16/XMODEM of the block (bas_crc16).
 * License: CC BY-NC-SA 4.0
 */
#include <string.h>
#include "cmd.h"
#include "ff.h"
#include "sd.h"

#define SD_BLOCK 128
#define W_OPEN   0x6F70
#define W_CREATE 0x6372
#define W_READ   0x7265
#define W_SEEK   0x7365
#define W_WRITE  0x7772
#define W_CLOSE  0x636C
#define W_DELETE 0x6465

static FIL f;
static uint8_t f_mode;        /* 0 closed, 1 reading, 2 writing */
static uint16_t f_block;      /* next block to read */
static char f_name[13];

static uint8_t valid_name(const char *n)
{
	uint8_t len = (uint8_t)strlen(n), dot = 0, base = 0, ext = 0;
	if (len == 0 || len > 12)
		return 0;
	for (const char *p = n; *p; p++) {
		char c = *p;
		if (c == '.') {
			if (dot++ || base == 0)
				return 0;
			continue;
		}
		if (c <= ' ' || strchr("\"*+,/:;<=>?[\\]|", c))
			return 0;
		if (dot) ext++; else base++;
	}
	return base <= 8 && ext <= 3;
}

/* FAT names do not distinguish case */
static uint8_t same_name(const char *a, const char *b)
{
	for (;; a++, b++) {
		char x = *a, y = *b;
		if (x >= 'a' && x <= 'z') x = (char)(x - 32);
		if (y >= 'a' && y <= 'z') y = (char)(y - 32);
		if (x != y)
			return 0;
		if (!x)
			return 1;
	}
}

static void close_file(void)
{
	if (f_mode) {
		f_close(&f);
		if (f_mode == 2)
			sd_preset_changed(f_name);
	}
	f_mode = 0;
}

static int8_t hexval(char c)
{
	if (c >= '0' && c <= '9') return (int8_t)(c - '0');
	if (c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
	if (c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
	return -1;
}

static void out_hex2(uint8_t b)
{
	static const char h[] = "0123456789ABCDEF";
	out_chr(h[b >> 4]);
	out_chr(h[b & 15]);
}

void sd_file_get(void)
{
	if (req_w(cmd_req_word_four) != 0x0000) {
		cmd_error_unknown();
		return;
	}
	if (f_mode != 1 || !sd_mounted()) {
		cmd_error_unknown();
		return;
	}
	uint8_t b[SD_BLOCK];
	UINT n = 0;
	if (f_lseek(&f, (FSIZE_t)f_block * SD_BLOCK) != FR_OK || f_read(&f, b, SD_BLOCK, &n) != FR_OK) {
		cmd_error_unknown();
		return;
	}
	cmd_answer_header();
	out_chr('[');
	out_u16(f_block);
	out_str(",\"");
	for (UINT i = 0; i < n; i++)
		out_hex2(b[i]);
	out_str("\",");
	out_u16(n ? bas_crc16(b, (uint16_t)n) : 0);
	out_chr(']');
	out_crlf();
	if (n)
		f_block++;
}

static uint8_t is_string(void) { return (cmd_reg_var_state & (1 << cmd_reg_state_isString)) != 0; }

uint8_t sd_file_set(void)
{
	uint16_t w = req_w(cmd_req_word_three);
	if (w != W_OPEN && w != W_CREATE && w != W_SEEK && w != W_WRITE && w != W_CLOSE && w != W_DELETE)
		return 0;
	if (req_w(cmd_req_word_four) != 0x0000) {
		cmd_error_unknown();
		return 1;
	}
	if (!sd_mounted()) {
		f_mode = 0;  /* card gone: the handle is void */
		cmd_error_unknown();
		return 1;
	}
	switch (w) {
	case W_OPEN:
	case W_CREATE:
	case W_DELETE:
		if (!is_string() || !valid_name(cmd_req_value)) {
			cmd_error_unknown_para();
			return 1;
		}
		if (w == W_DELETE) {
			if (f_mode && same_name(cmd_req_value, f_name)) {
				cmd_error_unknown();  /* close it first */
				return 1;
			}
			if (f_unlink(cmd_req_value) != FR_OK) {
				cmd_error_unknown();
				return 1;
			}
			break;
		}
		close_file();
		if (f_open(&f, cmd_req_value, w == W_OPEN ? FA_READ : FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
			cmd_error_unknown();
			return 1;
		}
		strcpy(f_name, cmd_req_value);
		f_mode = w == W_OPEN ? 1 : 2;
		f_block = 0;
		break;
	case W_SEEK:
		if (f_mode != 1 || cmd_reg_var_integers[1] < 0) {
			cmd_error_cmd_oor();
			return 1;
		}
		f_block = (uint16_t)cmd_reg_var_integers[1];
		break;
	case W_WRITE: {
		if (f_mode != 2 || !is_string()) {
			cmd_error_unknown();
			return 1;
		}
		uint8_t b[48];
		uint8_t n = 0;
		const char *p = cmd_req_value;
		for (; p[0] && p[0] != ':' && n < sizeof b; p += 2) {
			int8_t hi = hexval(p[0]), lo = p[1] ? hexval(p[1]) : -1;
			if (hi < 0 || lo < 0) {
				cmd_error_unknown_para();
				return 1;
			}
			b[n++] = (uint8_t)(hi << 4 | lo);
		}
		uint16_t crc = 0;
		uint8_t k = 0;
		if (*p == ':')
			for (p++; k < 4 && hexval(*p) >= 0; k++, p++)
				crc = (uint16_t)(crc << 4 | (uint8_t)hexval(*p));
		UINT done;
		if (k != 4 || *p || crc != bas_crc16(b, n) || f_write(&f, b, n, &done) != FR_OK || done != n) {
			cmd_error_unknown_para();
			return 1;
		}
		break;
	}
	case W_CLOSE:
		close_file();
		break;
	}
	cmd_answer_ok();
	return 1;
}
