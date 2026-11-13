/*
 * microSD on the USB(-audio) board, issue #24: SPIE (PE5 MOSI, PE6 MISO, PE7 SCK), CS PE4,
 * card switch PD0 (0 = card inserted), as libs/interface/V40/11 and libs/dos of the original.
 * The original used AVR-DOS (removed for the license); the port uses FatFs R0.16 (lib/fatfs).
 */
#ifndef SD_H_
#define SD_H_
#include <stdint.h>
extern uint8_t sd_ready;           /* global_state.sdFileRdy: card mounted */
uint8_t sd_card_present(void);     /* card switch */
void sd_init(void);                /* dos_init: SPI, card, mount */
void sd_preset_load_init(void);
void cmd_get_system_sd(void);    /* PORT (#24): get system.sd / system.sd.files */
void cmd_set_system_sd(void);    /* PORT (#24): set system.sd.export=N */
uint8_t sd_mounted(void);          /* card present and mounted (mounts a card inserted later) */
void sd_preset_changed(const char *name);  /* a preset_N.cfg was written: load it at the next start */
/* PORT (#59): file transfer over the command line (sd_file.c) */
void sd_file_get(void);            /* get system.sd.read */
uint8_t sd_file_set(void);         /* set system.sd.open/create/seek/write/close/delete, 0 = not ours */    /* libs/file/preset_load: preset_N.cfg -> preset N-1 */
#endif
