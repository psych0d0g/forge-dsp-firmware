/*
 * Port of libs/misc/registers, libs/misc/error_count and the EEPROM part of
 * devices/<flavour>/systeminit.bas (sysinit after a firmware change, boot counter).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "log.h"
#include <string.h>
#include "eram.h"
#include "power.h"
#include "registers.h"
#include "version.h"

uint8_t registers[BAS_MAX_REGISTERS + 1];
uint8_t error_count;
uint8_t error_count_type[BAS_ERROR_COUNT_MAX + 1];

void registers_load(void)
{
	for (uint8_t i = 1; i <= BAS_MAX_REGISTERS; i++)
		registers[i] = eram_rb(ERAM_EL(REGISTERS_ERAM, i));
}

void set_register(uint8_t reg, uint8_t state)
{
	registers[reg] = state;
	eram_wb(ERAM_EL(REGISTERS_ERAM, reg), state);
}

void reset_register(uint8_t reg)
{
	set_register(reg, 0x00);
}

void errorcount_init(void)
{
	for (uint8_t i = 1; i <= BAS_ERROR_COUNT_MAX; i++)
		error_count_type[i] = eram_rb(ERAM_EL(ERROR_COUNT_TYPE_ERAM, i));
}

/* PORT extension: set system.errors=0 empties the error counters (the original only by a
 * factory reset); written slots only, so an empty list costs no EEPROM writes */
void errorcount_clear(void)
{
	for (uint8_t i = 1; i <= BAS_ERROR_COUNT_MAX; i++) {
		if (error_count_type[i] == 0xFF)
			continue;
		error_count_type[i] = 0xFF;
		eram_wb(ERAM_EL(ERROR_COUNT_TYPE_ERAM, i), 0xFF);
		eram_ww(ERAM_EL(ERROR_COUNT_ERAM, i), 0xFFFF);
	}
	error_count = 0;
}

void errorcount(uint8_t errortype)
{
	uint8_t tfound = 0, efound = 0;
	if (errortype < 128)
		error_count++;  /* soft errors do not count as startup errors */
	for (uint8_t i = 1; i <= BAS_ERROR_COUNT_MAX; i++) {
		if (error_count_type[i] == errortype)
			tfound = i;
		if (efound == 0 && error_count_type[i] == 0xFF)
			efound = i;
	}
	uint16_t n = 0;
	if (tfound > 0) {
		uint16_t a = ERAM_EL(ERROR_COUNT_ERAM, tfound);
		n = (uint16_t)(eram_rw(a) + 1);
		eram_ww(a, n);
	} else if (efound > 0) {
		error_count_type[efound] = errortype;
		eram_wb(ERAM_EL(ERROR_COUNT_TYPE_ERAM, efound), errortype);
		eram_ww(ERAM_EL(ERROR_COUNT_ERAM, efound), 1);
		n = 1;
	}
	/* PORT (#60): soft errors (128+) as warnings */
	log_event(errortype < 128 ? LOG_ERROR : LOG_WARN, LOGM_ERR, errortype, n, 0);
	/* PORT: BASCOM writes index 0 when all slots are used, which overwrites the byte
	 * before error_count_type_eram and type slots 63/64 in the EEPROM; skipped here */
}

void sysinit_check(uint8_t dbg_init_finest)
{
	if (registers[BAS_REG_STARTUP_STATE] == BAS_STARTUP_STATE_SYSINIT) {
		for (uint8_t i = 1; i <= BAS_NULLBUFFER_SIZE; i++)
			eram_wb(ERAM_EL(NULLBUFFER_ERAM, i), BAS_NULLBUFFER_SEQ);
		/* version(2) of the BASCOM build; compared at every start (see below) */
		eram_write_str(ERAM_VERSION_REGISTER, ERAM_VERSION_REGISTER_SIZE, FW_COMPAT_VERSION);
		eram_ww(ERAM_DEVICE_BOOTCYCLES_ERAM, 0);
		for (uint8_t i = 1; i <= BAS_LIBS_MISC_CHANNEL_NAMES_MAX; i++)
			eram_write_str(ERAM_EL(CHANNEL_NAMES, i), ERAM_CHANNEL_NAMES_SIZE, "");
		if (dbg_init_finest) {
			set_register(BAS_REG_DEBUGMODE, BAS_DBGLVL_INIT_PIN);
			set_register(BAS_REG_DEBUGOUT, BAS_DEBUGOUT_INIT_PIN);
		} else {
			set_register(BAS_REG_DEBUGMODE, BAS_DBGLVL_INIT);
			set_register(BAS_REG_DEBUGOUT, BAS_DEBUGOUT_INIT);
		}
		set_register(BAS_REG_DEBUGHIDE, BAS_DBGLVL_INIT_HIDE);
	} else {
		/* "check if the last firmware update init a sysinit or not": a different
		 * version string resets all settings. FW_COMPAT_VERSION must therefore stay
		 * "1.11.6000" as long as the EEPROM layout is that of the BASCOM firmware. */
		char v[ERAM_VERSION_REGISTER_SIZE];
		eram_read_str(ERAM_VERSION_REGISTER, sizeof v, v);
		if (strcmp(v, FW_COMPAT_VERSION) != 0) {
			set_register(BAS_REG_STARTUP_STATE, BAS_STARTUP_STATE_SYSINIT);
			mcu_reset_delayed();
		}
	}
}

void sysinit_count_boot(void)
{
	eram_ww(ERAM_DEVICE_BOOTCYCLES_ERAM, (uint16_t)(eram_rw(ERAM_DEVICE_BOOTCYCLES_ERAM) + 1));
	log_boot_counted();  /* PORT (#60): earlier records already used this number */
}
