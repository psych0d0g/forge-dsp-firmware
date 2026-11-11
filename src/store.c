/*
 * Port of libs/store/cores/functions.bas and libs/eeprom/at24c512/init.bas.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "cmd.h"
#include "flavour_data.h"
#include "i2c.h"
#include "registers.h"
#include "store.h"

uint8_t storage_non_biquadvalues[BAS_STORAGE_FRAMESIZE + 1];
uint8_t store_preset;
uint16_t store_bq_nr;
uint16_t store_chksum_error;
uint16_t hardware_other;

#define STORE_RDY (hardware_other & (1u << BAS_AT24C512_STORE_RDY))
#define BUS 1  /* #5 */

void at24c512_detect(void)
{
	if (!(channel_open & (1u << 5)))
		return;
	twi_start(BUS);
	twi_wbyte(BUS, BAS_EEPROM_ADR_W);
	if (i2c_err == 0)
		hardware_other |= 1u << BAS_AT24C512_STORE_RDY;
	else
		errorcount((uint8_t)(BAS_AT24C512_STORE_RDY + BAS_ERRORCOUNT_OFFSET_OTHER));
	twi_stop(BUS);
}

uint16_t storage_chksum_get(void)
{
	return storage_non_biquadvalues[BAS_STORAGE_CHKSUM_BASE] |
	       (uint16_t)storage_non_biquadvalues[BAS_STORAGE_CHKSUM_BASE + 1] << 8;
}

static void address(uint16_t a)
{
	twi_wbyte(BUS, (uint8_t)(a >> 8));
	twi_wbyte(BUS, (uint8_t)(a & 0xFF));
}

void store_save_values_all(void)
{
	if (!STORE_RDY)
		return;
#if BAS_FEATURE_DATA_CHKSUM > 0
	uint16_t crc = bas_crc16(&storage_non_biquadvalues[1], BAS_STORAGE_CHKSUM_BASE - 1);
	storage_non_biquadvalues[BAS_STORAGE_CHKSUM_BASE] = crc & 0xFF;
	storage_non_biquadvalues[BAS_STORAGE_CHKSUM_BASE + 1] = crc >> 8;
#endif
	uint16_t a = BAS_LOOKUP_W(data_store_addr_values, store_preset);
	twi_start(BUS);
	twi_wbyte(BUS, BAS_EEPROM_ADR_W);
	address(a);
	uint16_t page = 0;
	for (uint16_t count = 1; count <= BAS_STORAGE_FRAMESIZE; count++) {
		twi_wbyte(BUS, storage_non_biquadvalues[count]);
		a++;
		page++;
		if (page > BAS_DSP_EEPROM_PAGE_SIZE) {  /* 128 bytes per write cycle */
			twi_stop(BUS);
			delay_ms(BAS_EEPROM_PAGE_WRITE_DELAY);
			twi_start(BUS);
			twi_wbyte(BUS, BAS_EEPROM_ADR_W);
			address(a);
			page = 0;
		}
	}
	twi_stop(BUS);
	delay_ms(BAS_EEPROM_PAGE_WRITE_DELAY);
}

void store_load_values_all(void)
{
	if (!STORE_RDY)
		return;
	uint16_t a = BAS_LOOKUP_W(data_store_addr_values, store_preset);
	twi_start(BUS);
	twi_wbyte(BUS, BAS_EEPROM_ADR_W);
	address(a);
	twi_repstart(BUS);
	twi_wbyte(BUS, BAS_EEPROM_ADR_W + 1);
	uint16_t count = 1;
	do {
		storage_non_biquadvalues[count] = twi_rbyte(BUS, 1);
		count++;
	} while (count != BAS_STORAGE_FRAMESIZE);
	storage_non_biquadvalues[count] = twi_rbyte(BUS, 0);
	twi_stop(BUS);
#if BAS_FEATURE_DATA_CHKSUM > 0
	uint16_t crc = bas_crc16(&storage_non_biquadvalues[1], BAS_STORAGE_CHKSUM_BASE - 1);
	if (storage_chksum_get() != crc) {
		store_chksum_error++;
		errorcount(BAS_ERRORCOUNT_FW_DATA_CHKSUM_SOFT);
	}
#endif
}

void store_save_bqtv(void)
{
	i2c_addr = (uint16_t)(BAS_LOOKUP_W(data_store_addr_bqtv, store_preset) +
	                      store_bq_nr * BAS_STORAGE_BQTV_VALUES_FRAMESIZE);
	i2c_size = BAS_STORAGE_BQTV_VALUES_FRAMESIZE;
	i2c_device_addr = BAS_EEPROM_ADR_W;
	i2c_wbuffer_eeprom(BUS);
}

void store_load_bqtv(void)
{
	i2c_addr = (uint16_t)(BAS_LOOKUP_W(data_store_addr_bqtv, store_preset) +
	                      store_bq_nr * BAS_STORAGE_BQTV_VALUES_FRAMESIZE);
	i2c_size = BAS_STORAGE_BQTV_VALUES_FRAMESIZE;
	i2c_device_addr = BAS_EEPROM_ADR_W;
	i2c_rbuffer_highreg(BUS);
}

void store_load_bqtype(void)
{
	uint16_t w = (uint16_t)((store_bq_nr + 1) * BAS_STORAGE_BQTV_VALUES_FRAMESIZE - 1);
	i2c_addr = (uint16_t)(BAS_LOOKUP_W(data_store_addr_bqtv, store_preset) + w);
	i2c_device_addr = BAS_EEPROM_ADR_W;
	i2c_rbyte_highreg(BUS);
}
