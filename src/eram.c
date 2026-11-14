/* Internal EEPROM access (config eeprom = mapped). License: CC BY-NC-SA 4.0 */
#include "eram.h"

uint8_t eram_v30;  /* PORT (#69): EEPROM layout of the V30-96ELN build, see eram.h */

void eram_read_str(uint16_t a, uint8_t size, char *dst)
{
	uint8_t i;
	for (i = 0; i + 1 < size; i++) {
		dst[i] = (char)eram_rb(a + i);
		if (!dst[i])
			return;
	}
	dst[i] = 0;
}

void eram_write_str(uint16_t a, uint8_t size, const char *s)
{
	uint8_t i;
	for (i = 0; i + 1 < size && s[i]; i++)
		eram_wb(a + i, (uint8_t)s[i]);
	eram_wb(a + i, 0);
}

#ifdef __AVR__
#include <avr/io.h>
#include <avr/interrupt.h>

static void nvm_wait(void)
{
	while (NVM.STATUS & NVM_NVMBUSY_bm)
		;
}

uint8_t eram_hw_read(uint16_t a)
{
	nvm_wait();
	return *(volatile uint8_t *)(MAPPED_EEPROM_START + a);
}

void eram_hw_write(uint16_t a, uint8_t v)
{
	nvm_wait();
	uint8_t sreg = SREG;
	cli();
	/* clear the page buffer, load one byte through the mapping, erase+write that page */
	NVM.CMD = NVM_CMD_ERASE_EEPROM_BUFFER_gc;
	CCP = CCP_IOREG_gc;
	NVM.CTRLA = NVM_CMDEX_bm;
	nvm_wait();
	NVM.CMD = NVM_CMD_NO_OPERATION_gc;
	*(volatile uint8_t *)(MAPPED_EEPROM_START + a) = v;  /* loads the page buffer */
	NVM.CMD = NVM_CMD_ERASE_WRITE_EEPROM_PAGE_gc;
	NVM.ADDR0 = a & 0xFF;
	NVM.ADDR1 = a >> 8;
	NVM.ADDR2 = 0;
	CCP = CCP_IOREG_gc;
	NVM.CTRLA = NVM_CMDEX_bm;
	NVM.CMD = NVM_CMD_NO_OPERATION_gc;
	SREG = sreg;
	nvm_wait();
}
#endif
