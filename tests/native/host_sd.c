/* microSD for the native tests: FatFs on a disk image file (512-byte sectors). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
#include "sd.h"

static FILE *img;
static int card;

uint8_t sd_card_present(void) { return (uint8_t)card; }
void host_sd_card(int on) { card = on; }

int host_sd_image(const char *path, unsigned mb)
{
	if (img)
		fclose(img);
	img = fopen(path, mb ? "w+b" : "r+b");
	if (!img)
		return -1;
	if (mb) {
		static BYTE work[FF_MAX_SS];
		fseek(img, (long)mb * 1024 * 1024 - 1, SEEK_SET);
		fputc(0, img);
		fflush(img);
		MKFS_PARM opt = {FM_FAT | FM_SFD, 0, 0, 0, 0};
		if (f_mkfs("", &opt, work, sizeof work) != FR_OK)
			return -2;
	}
	return 0;
}

/* file into the image, optional date (yyyy, mm, dd, hh, mi) */
int host_sd_put(const char *name, const char *hostfile, int y, int mo, int d, int h, int mi)
{
	FATFS fs;
	if (f_mount(&fs, "", 1) != FR_OK)
		return -1;
	FILE *src = fopen(hostfile, "rb");
	FIL f;
	if (!src || f_open(&f, name, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
		return -2;
	char buf[512];
	size_t n;
	UINT w;
	while ((n = fread(buf, 1, sizeof buf, src)) > 0)
		f_write(&f, buf, (UINT)n, &w);
	fclose(src);
	f_close(&f);
	if (y) {
		FILINFO fi;
		fi.fdate = (WORD)(((y - 1980) << 9) | (mo << 5) | d);
		fi.ftime = (WORD)((h << 11) | (mi << 5));
		f_utime(name, &fi);
	}
	f_mount(NULL, "", 0);
	return 0;
}

DSTATUS disk_status(BYTE drv) { return (drv || !img) ? STA_NOINIT : 0; }
DSTATUS disk_initialize(BYTE drv) { return (drv || !img) ? STA_NOINIT : 0; }

DRESULT disk_read(BYTE drv, BYTE *buf, LBA_t sector, UINT count)
{
	if (drv || !img)
		return RES_NOTRDY;
	fseek(img, (long)sector * 512, SEEK_SET);
	return fread(buf, 512, count, img) == count ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE drv, const BYTE *buf, LBA_t sector, UINT count)
{
	if (drv || !img)
		return RES_NOTRDY;
	fseek(img, (long)sector * 512, SEEK_SET);
	return fwrite(buf, 512, count, img) == count ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE drv, BYTE ctrl, void *buf)
{
	if (drv || !img)
		return RES_NOTRDY;
	if (ctrl == CTRL_SYNC) {
		fflush(img);
		return RES_OK;
	}
	if (ctrl == GET_SECTOR_COUNT) {
		long cur = ftell(img);
		fseek(img, 0, SEEK_END);
		*(LBA_t *)buf = (LBA_t)(ftell(img) / 512);
		fseek(img, cur, SEEK_SET);
		return RES_OK;
	}
	if (ctrl == GET_BLOCK_SIZE) {
		*(DWORD *)buf = 1;
		return RES_OK;
	}
	return RES_PARERR;
}
