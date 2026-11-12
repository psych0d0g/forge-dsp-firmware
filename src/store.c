/*
 * Port of libs/store/cores/functions.bas and libs/eeprom/at24c512/init.bas.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "cmd.h"
#include "flavour_data.h"
#include "i2c.h"
#include "registers.h"
#include "store.h"
#include "factory.h"
#include "biquad.h"
#include "dsp.h"
void watchdog_kick(void);

uint8_t storage_non_biquadvalues[BAS_STORAGE_FRAMESIZE + 1];
uint8_t store_preset;
uint16_t store_bq_nr;
uint16_t store_chksum_error;
uint16_t hardware_other;
uint8_t storage_flags, storage_flags_check;

#define STORE_RDY (hardware_other & (1u << BAS_AT24C512_STORE_RDY))
#define BUS 1  /* #5 */

static void address(uint16_t a);
void eeprom_init(void);

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
	eeprom_init();
}

uint8_t factory_reset_enabled = FORGE_FACTORY_RESET;

/* eeprom_clear_mem: the whole AT24C512 to 0xFF in pages of 128 bytes */
static void eeprom_clear_mem(void)
{
	uint16_t a = 0;
	uint8_t page = 0;
	twi_start(BUS);
	twi_wbyte(BUS, BAS_EEPROM_ADR_W);
	address(a);
	for (uint32_t count = 0; count <= BAS_EEPROM_CLEAR_COUNT; count++) {
		twi_wbyte(BUS, 0xFF);
		a++;
		page++;
		if (page > BAS_EEPROM_PAGE_SIZE) {
			twi_stop(BUS);
			delay_ms(BAS_EEPROM_PAGE_WRITE_DELAY);
			twi_start(BUS);
			twi_wbyte(BUS, BAS_EEPROM_ADR_W);
			address(a);
			page = 0;
			watchdog_kick();
		}
	}
	twi_stop(BUS);
	delay_ms(BAS_EEPROM_PAGE_WRITE_DELAY);
}

static void eeprom_check_mem(void)
{
	uint8_t e = 0;
	uint16_t w = 0;
	for (uint16_t frame = 0; frame <= BAS_EEPROM_CLEAR_COUNT / BAS_BUFFER_BYTES_MAX; frame++) {
		i2c_device_addr = BAS_EEPROM_ADR_W;
		i2c_addr = w;
		i2c_size = BAS_BUFFER_BYTES_MAX;
		i2c_rbuffer_highreg(BUS);
		for (uint8_t i = 1; i <= BAS_BUFFER_BYTES_MAX; i++)
			if (buffer_bytes[i] < 0xFF && e < 0xFF)
				e++;
		w = (uint16_t)(w + BAS_BUFFER_BYTES_MAX);
		watchdog_kick();
	}
	if (e)
		errorcount(BAS_ERRORCOUNT_SW_EEPROM);
}

/* libs/eeprom/at24c512 eeprom_init: erased and checked at a factory reset */
void eeprom_init(void)
{
	if (STORE_RDY && factory_write()) {
		eeprom_clear_mem();
		eeprom_check_mem();
	}
}

void store_init(void)
{
	storage_flags = 0;
	storage_flags_check = 0;
	if (factory_write()) {  /* preset 0 to defaults, copied to all presets, selected */
		store_preset = 0x00;
		store_erase_preset();
		for (uint8_t count = 1; count < BAS_MODUL_MAX_PRESET; count++) {
			store_preset_copy_from = 0;
			store_preset_copy_to = count;
			store_copy_preset();
		}
		store_set_preset();
	}
	store_preset = 0xFF;  /* preset not loaded */
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

void store_load_bq(void)
{
	uint8_t storage = BAS_LOOKUP_B(data_flow_dsp_bq_core, store_bq_nr);
	uint16_t w = (uint16_t)(store_bq_nr - BAS_LOOKUP_W(data_flow_bq_storage_offset, storage));
	w = (uint16_t)(w * BAS_DSP_REG_SIZE_BIQUAD_CHKSUM);
	i2c_addr = (uint16_t)(BAS_LOOKUP_W(data_store_addr_bq, store_preset) + w);
	i2c_size = BAS_DSP_REG_SIZE_BIQUAD_CHKSUM;
	switch (storage) {
	case 1: i2c_device_addr = 0xA0; i2c_rbuffer_highreg(1); break;  /* dsp_1_eeprom_adr_w */
	case 2: i2c_device_addr = 0xA0; i2c_rbuffer_highreg(2); break;  /* dsp_2_eeprom_adr_w */
	default: i2c_device_addr = 0xA2; i2c_rbuffer_highreg(2); break; /* dsp_3_eeprom_adr_w */
	}
	uint16_t crc = bas_crc16(&buffer_bytes[1], BAS_DSP_REG_SIZE_BIQUAD);
	if ((buffer_bytes[21] | (uint16_t)buffer_bytes[22] << 8) != crc) {  /* buffer_words(11) */
		store_chksum_error++;
		errorcount(BAS_ERRORCOUNT_FW_DATA_CHKSUM_SOFT);
	}
}

void store_restore_biquad_raw(void)
{
	for (store_bq_nr = 1; store_bq_nr <= BAS_FLOW_MAX_DSP_BIQUADS; store_bq_nr++) {
		uint32_t raw[6];
		dsp_bq_virt_nr = store_bq_nr;
		store_load_bqtype();
		if (buffer_byte > 0) {
			uint8_t e_count = 0;
			uint16_t e_store;
			do {
				e_store = store_chksum_error;
				store_load_bq();
				if (store_chksum_error > e_store) {
					if (e_count > 2) {
						errorcount(BAS_ERRORCOUNT_FW_DATA_CHKSUM_HARD);
						break;
					}
					e_count++;
				}
			} while (e_store != store_chksum_error);
			for (uint8_t i = 1; i <= 5; i++) {
				uint8_t at = (uint8_t)((i - 1) * 4 + 1);
				raw[i] = buffer_bytes[at] | (uint32_t)buffer_bytes[at + 1] << 8 |
				         (uint32_t)buffer_bytes[at + 2] << 16 | (uint32_t)buffer_bytes[at + 3] << 24;
			}
		} else {
			dsp_bq_default(raw);
		}
		dsp_calc_core();  /* dsp_set_biquad_calc */
		dsp_calc_reg();
		dsp_set_biquad(raw);
		watchdog_kick();  /* reset watchdog */
	}
}

void store_load_preset(void)
{
	store_restore_biquad_raw();
	uint8_t e_count = 0;
	uint16_t e_store;
	do {
		e_store = store_chksum_error;
		store_load_values_all();
		if (store_chksum_error > e_store) {
			if (e_count > 2) {
				errorcount(BAS_ERRORCOUNT_FW_PRESET_LOAD);
				break;
			}
			e_count++;
		}
	} while (e_store != store_chksum_error);
}

void store_set_preset(void)
{
	set_register(BAS_REG_LOAD_PRESET, store_preset);
}

/* save the values frame once the changes stop (one main loop without a new change) */
void store_mainloop(void)
{
	if (storage_flags > 0) {
		if (storage_flags < storage_flags_check) {
			storage_flags = 0;
			storage_flags_check = 0;
			store_save_values_all();
		}
		storage_flags_check++;
	}
}

/* --- preset copy / erase (libs/store/functions.bas, cores/<n>) --- */

uint8_t store_preset_copy_from, store_preset_copy_to;

static void copy_frames(uint8_t bus_from, uint8_t dev_from, uint8_t bus_to, uint8_t dev_to, uint16_t size,
                        uint16_t addr_from, uint16_t addr_to)
{
	for (uint16_t frame = 1; frame <= size / BAS_BUFFER_BYTES_MAX; frame++) {
		i2c_device_addr = dev_from;
		i2c_addr = addr_from;
		i2c_size = BAS_BUFFER_BYTES_MAX;
		i2c_rbuffer_highreg(bus_from);
		i2c_device_addr = dev_to;
		i2c_addr = addr_to;
		i2c_size = BAS_BUFFER_BYTES_MAX;
		i2c_wbuffer_eeprom(bus_to);
		addr_from += BAS_BUFFER_BYTES_MAX;
		addr_to += BAS_BUFFER_BYTES_MAX;
		watchdog_kick();
	}
}

#define COPY(tab, limit, bus, dev) \
	copy_frames(bus, dev, bus, dev, limit, BAS_LOOKUP_W(tab, store_preset_copy_from), BAS_LOOKUP_W(tab, store_preset_copy_to))

static const uint16_t copy_event[cmd_req_words_max] = {0x0104, 0x7072, 0x636F, 0x0000, 0x0000};
static const uint16_t erase_event[cmd_req_words_max] = {0x0104, 0x7072, 0x6572, 0x0000, 0x0000};

static void event_number(const uint16_t *ev, uint8_t v)
{
	uint8_t i = 0;
	if (v >= 100) cmd_req_value[i++] = (char)('0' + v / 100);
	if (v >= 10) cmd_req_value[i++] = (char)('0' + v / 10 % 10);
	cmd_req_value[i++] = (char)('0' + v % 10);
	cmd_req_value[i] = 0;
	cmd_load_header(ev);
	cmd_sendevent();
}

void store_copy_preset(void)
{
	COPY(data_store_addr_values, BAS_LIMIT_STORE_ADDR_VALUES, BUS, BAS_EEPROM_ADR_W);
	COPY(data_store_addr_bqcluster, BAS_LIMIT_STORE_ADDR_BQCLUSTER, BUS, BAS_EEPROM_ADR_W);
	COPY(data_store_addr_bq, BAS_LIMIT_STORE_ADDR_BQ, 1, 0xA0);      /* dsp_1_eeprom_adr_w */
#if BAS_MODUL_TAS3108_CORES >= 2
	COPY(data_store_addr_bq, BAS_LIMIT_STORE_ADDR_BQ, 2, 0xA0);      /* dsp_2_eeprom_adr_w */
#endif
#if BAS_MODUL_TAS3108_CORES >= 3
	COPY(data_store_addr_bq, BAS_LIMIT_STORE_ADDR_BQ, 2, 0xA2);      /* dsp_3_eeprom_adr_w */
#endif
	COPY(data_store_addr_bqtv, BAS_LIMIT_STORE_ADDR_BQTV, BUS, BAS_EEPROM_ADR_W);
	if (registers[BAS_REG_STARTUP_STATE] == BAS_STARTUP_STATE_DONE)
		event_number(copy_event, store_preset_copy_to);
}

#define SV(base, i) storage_non_biquadvalues[(base) + (i) - 1]
static void sw_put(uint16_t base, uint8_t i, uint16_t v)
{
	uint16_t a = (uint16_t)(base + (i - 1) * 2);
	storage_non_biquadvalues[a] = v & 0xFF;
	storage_non_biquadvalues[a + 1] = v >> 8;
}

void store_reset_values(void)
{
	storage_non_biquadvalues[BAS_STORE_MIX_POSITION_BASE] = BAS_STORE_MIX_POSITION_DEFAULT;
	for (uint16_t wi = 1; wi <= BAS_STORE_MIX_POSITIONS * BAS_STORE_MIX_FRAME; wi++)
		SV(BAS_STORAGE_MIX_BASE, wi) = BAS_STORE_MIX_DEFAULT;
	uint16_t n = BAS_LOOKUP_W(data_flow_mix_passthrough, 0);  /* length of the data field */
	for (uint16_t wi = 1; wi <= n; wi++)
		SV(BAS_STORAGE_MIX_BASE, BAS_LOOKUP_W(data_flow_mix_passthrough, wi)) = 100;
	for (uint8_t i = 1; i <= BAS_STORE_MUX_COUNT; i++)
		SV(BAS_STORAGE_MUX_BASE, i) = BAS_STORE_MUX_DEFAULT;
	for (uint8_t i = 1; i <= BAS_STORE_VOL_IN_COUNT; i++)
		SV(BAS_STORAGE_VOL_IN_BASE, i) = BAS_STORE_VOL_IN_DEFAULT;
	for (uint8_t i = 1; i <= BAS_STORE_VOL_OUT_COUNT; i++)
		SV(BAS_STORAGE_VOL_OUT_BASE, i) = BAS_STORE_VOL_OUT_DEFAULT;
	for (uint8_t i = 1; i <= BAS_STORE_VOL_SPDIF_COUNT; i++)
		SV(BAS_STORAGE_VOL_SPDIF_BASE, i) = BAS_STORE_VOL_SPDIF_DEFAULT;
	storage_non_biquadvalues[BAS_STORAGE_MUTE_OUT_BASE] = BAS_STORE_MUTE_OUT_DEFAULT;
	storage_non_biquadvalues[BAS_STORAGE_PHASE_BASE] = BAS_STORE_PHASE_DEFAULT;
#ifdef BAS_STORE_DELAY_IN_COUNT
	for (uint8_t i = 1; i <= BAS_STORE_DELAY_IN_COUNT; i++)
		sw_put(BAS_STORAGE_DELAY_IN_BASE, i, BAS_STORE_DELAY_DEFAULT);
#endif
	for (uint8_t i = 1; i <= BAS_STORE_DELAY_COUNT; i++)
		sw_put(BAS_STORAGE_DELAY_BASE, i, BAS_STORE_DELAY_DEFAULT);
	for (uint8_t i = 1; i <= BAS_STORE_SELECTOR_COUNT; i++) {
		SV(BAS_STORAGE_SELECTOR_BASE, i) = BAS_LOOKUP_B(data_flow_selector_type, i);
		uint16_t a = (uint16_t)((i - 1) * BAS_FLOW_MAX_SELECTOR_PARA);
		for (uint8_t ii = 1; ii <= BAS_FLOW_MAX_SELECTOR_PARA; ii++)
			SV(BAS_STORAGE_SELECTOR_PARA_BASE, ++a) = BAS_LOOKUP_B(data_flow_selector_para_type, ii);
	}
	for (uint8_t i = 1; i <= BAS_STORE_GAIN_COUNT; i++) {
		SV(BAS_STORAGE_GAIN_BASE, i) = BAS_LOOKUP_B(data_flow_gain_type, i);
		uint16_t a = (uint16_t)((i - 1) * BAS_FLOW_MAX_GAIN_PARA);
		for (uint8_t ii = 1; ii <= BAS_FLOW_MAX_GAIN_PARA; ii++)
			sw_put(BAS_STORAGE_GAIN_PARA_BASE, (uint8_t)++a, BAS_LOOKUP_W(data_flow_gain_para_type, ii));
	}
}

void store_save_bq(void)
{
	uint16_t crc = bas_crc16(&buffer_bytes[1], BAS_DSP_REG_SIZE_BIQUAD);
	buffer_bytes[21] = crc & 0xFF;  /* buffer_words(11) */
	buffer_bytes[22] = crc >> 8;
	uint8_t storage = BAS_LOOKUP_B(data_flow_dsp_bq_core, store_bq_nr);
	uint16_t w = (uint16_t)(store_bq_nr - BAS_LOOKUP_W(data_flow_bq_storage_offset, storage));
	i2c_addr = (uint16_t)(BAS_LOOKUP_W(data_store_addr_bq, store_preset) + w * BAS_DSP_REG_SIZE_BIQUAD_CHKSUM);
	i2c_size = BAS_DSP_REG_SIZE_BIQUAD_CHKSUM;
	switch (storage) {
	case 1: i2c_device_addr = 0xA0; i2c_wbuffer_eeprom(1); break;
	case 2: i2c_device_addr = 0xA0; i2c_wbuffer_eeprom(2); break;
	default: i2c_device_addr = 0xA2; i2c_wbuffer_eeprom(2); break;
	}
}

static void fill_default_bq(void)
{
	uint32_t raw[6];
	dsp_bq_default(raw);  /* store_reset_bq_cache */
	/* buffer_bytes is also the I2C buffer: refill before every write */
	for (uint8_t i = 1; i <= 5; i++)
		for (uint8_t k = 0; k < 4; k++)
			buffer_bytes[(i - 1) * 4 + 1 + k] = (uint8_t)(raw[i] >> (8 * k));
}

static void fill_default_bqtv(void)
{
	buffer_bytes[1] = BAS_DSP_BQTYPE_ALL_PASS;  /* store_reset_bqtv_cache */
	for (uint8_t i = 2; i <= 8; i++)
		buffer_bytes[i] = 0;
	buffer_bytes[9] = 0;   /* still 0 from the biquad cache in the original */
	buffer_bytes[10] = 0;
}

void store_erase_preset(void)
{
	store_reset_values();
	store_save_values_all();
	for (store_bq_nr = 1; store_bq_nr <= BAS_FLOW_MAX_DSP_BIQUADS; store_bq_nr++) {
		fill_default_bq();
		store_save_bq();
		watchdog_kick();
	}
	for (store_bq_nr = 1; store_bq_nr <= BAS_FLOW_MAX_DSP_BIQUADS; store_bq_nr++) {
		fill_default_bqtv();
		store_save_bqtv();
		watchdog_kick();
	}
	event_number(erase_event, store_preset);
}

/* PORT (#47): place of the stored biquad nr of a preset (as store_load_bq/store_save_bq) -> bus */
static uint8_t bq_place(uint16_t nr, uint8_t preset)
{
	uint8_t storage = BAS_LOOKUP_B(data_flow_dsp_bq_core, nr);
	uint16_t w = (uint16_t)(nr - BAS_LOOKUP_W(data_flow_bq_storage_offset, storage));
	i2c_addr = (uint16_t)(BAS_LOOKUP_W(data_store_addr_bq, preset) + w * BAS_DSP_REG_SIZE_BIQUAD_CHKSUM);
	i2c_size = BAS_DSP_REG_SIZE_BIQUAD_CHKSUM;
	switch (storage) {
	case 1: i2c_device_addr = 0xA0; return 1;
	case 2: i2c_device_addr = 0xA0; return 2;
	default: i2c_device_addr = 0xA2; return 2;
	}
}

static void bqtv_place(uint16_t nr, uint8_t preset)
{
	i2c_addr = (uint16_t)(BAS_LOOKUP_W(data_store_addr_bqtv, preset) + nr * BAS_STORAGE_BQTV_VALUES_FRAMESIZE);
	i2c_size = BAS_STORAGE_BQTV_VALUES_FRAMESIZE;
	i2c_device_addr = BAS_EEPROM_ADR_W;
}

void store_bq_move(uint16_t from, uint16_t to)
{
	for (uint8_t p = 0; p < BAS_MODUL_MAX_PRESET; p++) {
		uint8_t bus = bq_place(from, p);
		i2c_rbuffer_highreg(bus);
		bq_place(to, p);
		i2c_wbuffer_eeprom(bus);
		bqtv_place(from, p);
		i2c_rbuffer_highreg(BUS);
		bqtv_place(to, p);
		i2c_wbuffer_eeprom(BUS);
		watchdog_kick();
	}
}

void store_bq_clear(uint16_t nr)
{
	for (uint8_t p = 0; p < BAS_MODUL_MAX_PRESET; p++) {
		fill_default_bq();
		uint16_t crc = bas_crc16(&buffer_bytes[1], BAS_DSP_REG_SIZE_BIQUAD);
		buffer_bytes[21] = crc & 0xFF;
		buffer_bytes[22] = crc >> 8;
		i2c_wbuffer_eeprom(bq_place(nr, p));
		fill_default_bqtv();
		bqtv_place(nr, p);
		i2c_wbuffer_eeprom(BUS);
		watchdog_kick();
	}
}
