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
		store_save_values_all();
		buart_channel = ch;
		storage_flags = 0;
		storage_flags_check = 0;
	}
	store_preset = 0xFF;  /* PORT: let MODC load the selected preset (see above) */
}

/* FatFs needs a time with FF_FS_NORTC = 0 only; kept for a later RTC */
