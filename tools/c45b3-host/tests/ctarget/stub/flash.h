#ifndef FLASH_H_
#define FLASH_H_
#include <stdint.h>
void flashPageFill(uint32_t address, uint8_t *buffer, unsigned int length);
void flashPageWrite(uint32_t address);
#endif
