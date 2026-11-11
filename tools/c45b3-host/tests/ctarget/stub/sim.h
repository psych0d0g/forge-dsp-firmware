/* Host stubs for running the original chip45boot3 sources natively. */
#ifndef SIM_H_
#define SIM_H_
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIM_APP_SIZE   0x40000UL   /* ATxmega256A3U application section */
#define SIM_BOOT_SIZE  0x2000UL
#define SIM_FLASH_SIZE (SIM_APP_SIZE + SIM_BOOT_SIZE)
#define E2END          0xFFF

extern uint8_t sim_flash[SIM_FLASH_SIZE];
extern uint8_t sim_eeprom[E2END + 1];
#endif
