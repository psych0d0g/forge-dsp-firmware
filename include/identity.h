/*
 * Device identity (#31): one build serves several identical variants that only differ in
 * the identity strings (V40/V41, 96ELN/CORED8, see docs/single-firmware.md). The identity
 * (model + PCB) is chosen from the list of the build and kept in the EEPROM (0xF00, unused by
 * the original layout); the flasher copies it from the firmware that was on the device.
 */
#ifndef IDENTITY_H_
#define IDENTITY_H_
#include <stdint.h>
const char *identity_model(void);   /* model_id: 96ELN, CORED8, ... */
const char *identity_board(void);   /* board_version: 4.0, 4.1, ... */
uint8_t identity_count(void);
const char *identity_name(uint8_t i, char buf[16]);  /* "MODEL-PCB" */
uint8_t identity_set(const char *model_pcb);         /* 0 = not allowed for this build */
#endif
