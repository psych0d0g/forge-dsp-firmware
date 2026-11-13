/*
 * microSD: mount (libs/dos dos_init) and the preset loader of the original
 * (libs/file/preset_load, preset_load_init), with FatFs instead of AVR-DOS (issue #24).
 *
 * At the start every file preset_1.cfg .. preset_N.cfg (N = number of presets) whose date/time
 * differs from the one stored in the EEPROM is loaded into preset 1..N (0-based 0..N-1):
 *   a line "[...<model_id>...]" (section of a GUI .cfg file) erases the preset, a section
 *   without the model ends the file; every line  key = "<register>=<value>(;cs)"  is executed
 *   as "set <register>=<value>(;cs)" by the text parser, answers go to the USB port.
 * PORT: store_preset is reset afterwards, so the regular preset load of MODC loads the
 * selected preset into the DSPs (the original leaves store_preset at the last loaded file
 * and then skips that load when it is the selected preset).
 * PORT: the date/time of a file is stored where the original layout has room for it
 * (V30-96LN: file_datetimes_eram); the other flavours use EEPROM 0xE00.. (unused there).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include <stdlib.h>
#include <string.h>
#include "sd.h"
#include "api.h"
#include "cmd.h"
#include "eram.h"
#include "ff.h"
#include "flavour.h"
#include "iface.h"
#include "identity.h"
#include "store.h"
#include "log.h"

uint8_t sd_ready;
static FATFS fs;

#ifdef ERAM_FILE_DATETIMES_ERAM
#define DT_ADDR(i) ERAM_EL(FILE_DATETIMES_ERAM, i)
#else
#define DT_ADDR(i) ((uint16_t)(0x0E00 + ((i) - 1) * 18))
#endif
#define DT_LEN 17  /* "dd.mm.yy hh:mm:ss" as BASCOM filedatetime() */

void sd_init(void)
{
	sd_ready = 0;
	if (!sd_card_present())
		return;
	if (f_mount(&fs, "", 1) == FR_OK)
		sd_ready = 1;  /* global_state.sdFileRdy */
}

uint8_t sd_mounted(void)
{
	if (!sd_card_present())
		sd_ready = 0;
	else if (!sd_ready)
		sd_init();
	return sd_ready;
}

/* preset_N.cfg written over the serial line (sd_file.c): forget the stored date, so the next
 * start loads it (without a clock every written file gets the same date) */
void sd_preset_changed(const char *name)
{
	for (uint8_t i = 1; i <= BAS_MODUL_MAX_PRESET && i <= 9; i++) {
		char n[] = "PRESET_x.CFG";
		n[7] = (char)('0' + i);
		uint8_t eq = 1;
		for (uint8_t k = 0; n[k] || name[k]; k++) {
			char c = name[k];
			if (c >= 'a' && c <= 'z')
				c = (char)(c - 'a' + 'A');
			if (c != n[k]) { eq = 0; break; }
		}
		if (eq)
			eram_write_str(DT_ADDR(i), DT_LEN + 1, "");
	}
}

static void two(char *p, uint8_t v)
{
	p[0] = (char)('0' + v / 10 % 10);
	p[1] = (char)('0' + v % 10);
}

static uint8_t file_datetime(const char *name, char out[DT_LEN + 1])
{
	FILINFO fi;
	if (f_stat(name, &fi) != FR_OK)
		return 0;
	two(out, fi.fdate & 31); out[2] = '.';
	two(out + 3, (fi.fdate >> 5) & 15); out[5] = '.';
	two(out + 6, (uint8_t)((1980 + (fi.fdate >> 9)) % 100)); out[8] = ' ';
	two(out + 9, (uint8_t)(fi.ftime >> 11)); out[11] = ':';
	two(out + 12, (fi.ftime >> 5) & 63); out[14] = ':';
	two(out + 15, (uint8_t)((fi.ftime & 31) * 2));
	out[DT_LEN] = 0;
	return 1;
}

static void run_line(char *line)
{
	char *q = strstr(line, " = \"");
	if (!q)
		return;
	char *v = q + 4;
	/* delchars "": all quotes removed */
	char buf[cmd_req_value_size + 8] = "set ";
	uint8_t n = 4;
	for (; *v && n < sizeof buf - 1; v++)
		if (*v != '"')
			buf[n++] = *v;
	buf[n] = 0;
	cmd_run_line(buf);
}

void sd_preset_load_init(void)
{
	if (!sd_ready)
		return;
	api_set_out_mute_change();
	for (uint8_t i = 1; i <= BAS_MODUL_MAX_PRESET && i <= 9; i++) {
		char name[] = "preset_x.cfg";
		name[7] = (char)('0' + i);
		char dt[DT_LEN + 1], stored[DT_LEN + 1];
		if (!file_datetime(name, dt))
			continue;
		eram_read_str(DT_ADDR(i), sizeof stored, stored);
		if (memcmp(dt, stored, DT_LEN) == 0)
			continue;  /* unchanged since the last load */
		eram_write_str(DT_ADDR(i), DT_LEN + 1, dt);
		FIL f;
		if (f_open(&f, name, FA_READ) != FR_OK)
			continue;
		store_preset = (uint8_t)(i - 1);
		uint8_t ch = buart_channel;
		buart_channel = iface_buart_channel[iface_ch_usb];
		char line[cmd_req_value_size + 8];
		while (f_gets(line, sizeof line, &f)) {
			size_t l = strlen(line);
			while (l && (line[l - 1] == '\r' || line[l - 1] == '\n'))
				line[--l] = 0;
			if (!l)
				continue;
			if (line[0] == '[') {
				if (!strstr(line, identity_model()))
					break;  /* section of another model */
				store_erase_preset();
			}
			run_line(line);
		}
		f_close(&f);
		log_event(LOG_INFO, LOGM_PRESET, LOGE_PRESET_SD, i, 0);  /* PORT (#60) */
		store_save_values_all();
		buart_channel = ch;
		storage_flags = 0;
		storage_flags_check = 0;
	}
	store_preset = 0xFF;  /* PORT: let MODC load the selected preset (see above) */
}

/* FatFs needs a time with FF_FS_NORTC = 0 only; kept for a later RTC */

/* PORT extension (#24 diagnostics), read-only:
 *   get system.sd       = [card present, mounted, FAT type 1/2/3 = FAT12/16/32 (0 none), size in MB]
 *   get system.sd.files = ["NAME.EXT:bytes", …] of the root directory (8.3 names, at most 32)
 * A card inserted after the start is mounted on the first request. No free space: on FAT32
 * without valid FSINFO that would scan the whole FAT (longer than the watchdog). */
void cmd_get_system_sd(void)
{
	uint16_t w3 = req_w(cmd_req_word_three);
	if (w3 == 0x7265) {
		sd_file_get();  /* read (sd_file.c) */
		return;
	}
	if ((w3 != 0x0000 && w3 != 0x6669) || req_w(cmd_req_word_four) != 0x0000) {
		cmd_error_unknown();
		return;
	}
	uint8_t present = sd_card_present();
	sd_mounted();
	cmd_answer_header();
	out_chr('[');
	if (w3 == 0x0000) {
		uint8_t t = sd_ready ? fs.fs_type : 0;
		uint32_t mb = sd_ready ? (uint32_t)(fs.n_fatent - 2) * fs.csize / 2048u : 0;
		out_u16(present); out_chr(',');
		out_u16(sd_ready); out_chr(',');
		out_u16(t == FS_FAT12 ? 1 : t == FS_FAT16 ? 2 : t == FS_FAT32 ? 3 : 0); out_chr(',');
		out_u32(mb);
	} else if (sd_ready) {
		DIR d;
		FILINFO fi;
		uint8_t n = 0;
		if (f_opendir(&d, "/") == FR_OK) {
			while (n < 32 && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
				if (fi.fattrib & (AM_DIR | AM_HID | AM_SYS))
					continue;
				if (n++)
					out_chr(',');
				out_chr('"');
				out_str(fi.fname);
				out_chr(':');
				out_u32(fi.fsize);
				out_chr('"');
			}
			f_closedir(&d);
		}
	}
	out_chr(']');
	out_crlf();
}

/* PORT extension (#24): set system.sd.export=N writes the loaded preset and the device settings
 * as preset_N.cfg (N 1..9) in the format the loader above and the GUI read: the get commands of
 * c45b3 export-tuning run internally, their answer lines become  FILE_v10 = "<register>=<value>".
 * Values of never written storage are left out, stored biquads are written as .calc (the
 * device recomputes them). The file date goes to the EEPROM, so the next start does not load the
 * file back; a file changed on the PC is loaded as before. */
static const char *const export_queries[] = {
	"mixer.position", "mixer.*", "gain.in.*", "gain.spdif.*", "gain.out.*", "mute.out.*",
	"phase.out.*", "delay.*", "delay.in.*", "system.channel.name.*", "extender.ir.custom.*",
	"modc.*", "biquad.*",
};
static FIL *xf;
static char xline[cmd_req_value_size + 64];
static uint8_t xn, xover, xerr;

static uint8_t x_blank(const char *reg, const char *v)
{
	if (!strncmp(reg, "biquad.", 7))
		return !strncmp(v, "[255,", 5);
	if (!strncmp(reg, "modc.", 5))
		return 0;
	return !strcmp(v, "255") || !strcmp(v, "32767") || !strcmp(v, "65535") || !strcmp(v, "\"\"");
}

static void x_line(void)
{
	char *p = xline;
	while (*p == ' ')
		p++;
	if (*p != '<')
		return;  /* errors, events, info */
	p = strchr(p, ' ');
	if (!p)
		return;
	p++;
	char *eq = strchr(p, '=');
	if (!eq)
		return;
	*eq = 0;
	const char *v = eq + 1;
	if (!strcmp(p, "biquad") || !strcmp(p, "modc.enable") || x_blank(p, v))
		return;
#ifdef BAS_STORE_DELAY_IN_COUNT
	/* get delay.in.* lists all 8 inputs like the original, but only the analog ones have a delay
	 * (7/8 are the digital inputs); set refuses the others */
	if (!strncmp(p, "delay.in.", 9) && atoi(p + 9) > BAS_STORE_DELAY_IN_COUNT)
		return;
#endif
	/* biquad.C.B -> biquad.C.B.calc */
	uint8_t dots = 0;
	for (const char *q = p; *q; q++)
		dots += *q == '.';
	f_puts("FILE_v10 = \"", xf);
	f_puts(p, xf);
	if (!strncmp(p, "biquad.", 7) && dots == 2)
		f_puts(".calc", xf);
	f_putc('=', xf);
	f_puts(v, xf);
	if (f_puts("\"\n", xf) < 0)
		xerr = 1;
}

static void x_chr(char c)
{
	if (c == '\r')
		return;
	if (c == '\n') {
		xline[xn] = 0;
		if (!xover)
			x_line();
		xn = xover = 0;
		iface_hook_watchdog();
		return;
	}
	if (xn < sizeof xline - 1)
		xline[xn++] = c;
	else
		xover = 1;
}

void cmd_set_system_sd(void)
{
	uint8_t n = (uint8_t)cmd_reg_var_integers[1];
	if (sd_file_set())
		return;  /* open, create, seek, write, close, delete (sd_file.c) */
	if (req_w(cmd_req_word_three) != 0x6578 || req_w(cmd_req_word_four) != 0x0000) {
		cmd_error_unknown();
		return;
	}
	if (n < 1 || n > BAS_MODUL_MAX_PRESET || n > 9) {
		cmd_error_cmd_oor();
		return;
	}
	if (!sd_mounted()) {
		cmd_error_unknown();
		return;
	}
	char name[] = "preset_x.cfg";
	name[7] = (char)('0' + n);
	static FIL f;
	if (f_open(&f, name, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
		cmd_error_unknown();
		return;
	}
	xf = &f;
	xn = xover = xerr = 0;
	f_puts("; exported by the Forge port firmware (set system.sd.export)\n[", &f);
	f_puts(identity_model(), &f);
	f_puts("]\n", &f);
	/* the answers of the internal requests go into the file; the request itself was parsed into
	 * the shared request buffer, so keep its answer header for the end */
	static uint8_t req[sizeof cmd_mem];
	memcpy(req, cmd_mem, sizeof req);
	out_capture = x_chr;
	for (uint8_t i = 0; i < sizeof export_queries / sizeof export_queries[0]; i++) {
		char q[40] = "get ";
		strcat(q, export_queries[i]);
		cmd_run_line(q);
	}
	out_capture = 0;
	memcpy(cmd_mem, req, sizeof req);
	uint8_t ok = f_close(&f) == FR_OK && !xerr;
	char dt[DT_LEN + 1];
	if (ok && file_datetime(name, dt))
		eram_write_str(DT_ADDR(n), DT_LEN + 1, dt);
	if (ok)
		cmd_answer_ok();
	else
		cmd_error_unknown();
}
