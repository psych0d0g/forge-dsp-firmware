#ifndef VERSION_H_
#define VERSION_H_
/* normally set by scripts/version.py from git (tag vX.Y.Z) */
#ifndef FW_VERSION_STR
#define FW_VERSION_STR "0.0.0-local"
#endif
/* the bootloader (command 0x12) reads "FW_VERSION:MM.mm" from flash */
#ifndef FW_VERSION_C45B3
#define FW_VERSION_C45B3 "FW_VERSION:00.00"
#endif
#ifndef FW_BUILD_DATE
#define FW_BUILD_DATE "00000000"  /* main_build_date in system.key.info */
#endif
/* system.firmware: protocol level of the BASCOM firmware this port implements;
 * the GUI enables features by this number. Own version: system.firmware.port */
#define FW_COMPAT_VERSION "1.11.6000"
#endif
