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
void cmd_set_system_sd(void);    /* PORT (#24): set system.sd.export=N */    /* libs/file/preset_load: preset_N.cfg -> preset N-1 */
#endif
