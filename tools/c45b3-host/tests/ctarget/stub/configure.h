/* replaces bootloader/configure.h + mcu.h for the native harness */
#ifndef CONFIGURE_H_
#define CONFIGURE_H_
#include "sim.h"
#define XMEGA
#define PGM_FAR
#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define BOOT_SECTION_START SIM_APP_SIZE
#define SPM_PAGESIZE 512
#endif
