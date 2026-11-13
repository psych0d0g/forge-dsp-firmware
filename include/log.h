/*
 * PORT extension (#60): binary event log on the microSD, the compact form of the original
 * debug.log (libs/debug debugout to file #99). Switched like the original: system.debug.out bit 2
 * (debugout_file) and system.debug.mode = level (both stored in the EEPROM). Format: docs/log-format.md
 */
#ifndef LOG_H_
#define LOG_H_
#include <stdint.h>

/* levels as dbglvl_* of the original; an event is written if level <= system.debug.mode */
#define LOG_FATAL 1
#define LOG_ERROR 2
#define LOG_WARN  3
#define LOG_INFO  4
#define LOG_DEBUG 5
#define LOG_TRACE 6

/* modules */
#define LOGM_SYS    1   /* start, restarts, hardware check */
#define LOGM_ERR    2   /* error counter: event = error type, p1 = count after it */
#define LOGM_DSP    3
#define LOGM_PRESET 4
#define LOGM_SUPPLY 5
#define LOGM_LOG    6

/* events */
#define LOGE_SYS_START       1  /* p1 = MCU reset flags (RST.STATUS), p2 = boot cycles high word (0) */
#define LOGE_SYS_DSP_RESTART 2  /* p1 = hardware_dsp found, p2 = a core reported an error state */
#define LOGE_SYS_HW_MISMATCH 3  /* p1 = hardware_dsp, p2 = hardware_other */
#define LOGE_DSP_EEPROM      1  /* p1 = core, p2 = verify (1 ok, 2 wrong) | written << 8 */
#define LOGE_DSP_CORE        2  /* p1 = core, p2 = state byte 1 | found << 8 */
#define LOGE_DSP_BUS_CLEARED 3  /* SDA low after the EEPROM work: p1 = bus, p2 = core */
#define LOGE_PRESET_LOADED   1  /* p1 = preset (0-based) */
#define LOGE_PRESET_SD       2  /* p1 = file number N of preset_N.cfg */
#define LOGE_SUPPLY_LOW      1  /* p1 = supply mV */
#define LOGE_SUPPLY_OK       2  /* p1 = supply mV */
#define LOGE_LOG_FILE        1  /* first record of a file: p1 = format version, p2 = record size */
#define LOGE_LOG_DROPPED     2  /* p1 = records lost (queue full or no card) */

#define LOG_FORMAT 1
#define LOG_RECORD 12
#define LOG_FILE "FORGE.LOG"
#define LOG_OLD  "FORGE.OLD"
#ifndef LOG_MAX_BYTES
#define LOG_MAX_BYTES 65536UL  /* the native tests use 240 */
#endif

void log_event(uint8_t level, uint8_t module, uint8_t event, uint16_t p1, uint16_t p2);
void log_mainloop(void);           /* writes the queue to the card; keeps the 32-bit clock */
uint8_t log_enabled(void);
uint32_t log_now_ms(void);
void log_boot_counted(void);       /* after sysinit_count_boot */
#endif
