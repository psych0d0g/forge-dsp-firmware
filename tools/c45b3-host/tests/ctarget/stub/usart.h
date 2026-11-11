#ifndef _USART_
#define _USART_
#include <stdint.h>
void usartInit(uint32_t);
void usartDeInit(void);
void usartPutChar(uint8_t);
uint8_t usartGetChar(uint8_t *);
#endif
