/*
 * Port of libs/clock/si5338 (init.bas, functions.bas). Frequencies in 1/10 Hz.
 * PORT: the BASCOM double arithmetic is done in 64-bit integers. All values are integers
 * below 2^53 and int() truncates, so the results are identical (checked against an IEEE
 * double emulation in tests/test_cmd.py). The subtraction gcd (si5338_math_ggt) is replaced
 * by Euclid with modulo: same result, without the 10^8 loop iterations that the original
 * needs for awkward values.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "flavour.h"
#if BAS_MODUL_SI5338 > 0
#include "bas_data.h"
#include "eram.h"
#include "flavour_data.h"
#include "i2c.h"
#include "registers.h"
#include "si5338.h"
#include "store.h"

#if BAS_MODUL_SI5338_I2C == 1
#define BUS 1
#else
#define BUS 2
#endif
#define si5338_max_drivers 4
#define FVCO_DEFAULT 25000000000ULL  /* si5338_Fvco_default, 1/10 Hz */

static uint8_t si5338_adr;
static uint8_t si5338_number;
uint64_t si5338_Fvco;
uint16_t si5338_MSx_A;
uint32_t si5338_MSx_B, si5338_MSx_C, si5338_MSx_P1, si5338_MSx_P2, si5338_MSx_P3;

static void wreg(uint8_t reg, uint8_t v)
{
	i2c_device_addr = si5338_adr;
	i2c_reg = reg;
	buffer_byte = v;
	i2c_wbyte(BUS);
}

static uint8_t rreg(uint8_t reg)
{
	i2c_device_addr = si5338_adr;
	i2c_reg = reg;
	i2c_rbyte(BUS);
	return buffer_byte;
}

static void si5338_page_0(void) { wreg(0xFF, 0x00); }

static void si5338_pre_load(void)
{
	wreg(230, 0x10);  /* OEB_ALL = 1 */
	wreg(241, 0x80);  /* DIS_LOL = 1 */
}

static void si5338_load_data(void)
{
	i2c_device_addr = si5338_adr;
	i2c_wbyte_sequence(BUS, si5338_data);
	si5338_page_0();
}

static uint32_t gcd32(uint32_t a, uint32_t b)
{
	/* si5338_math_ggt: buffer_dwords(1) = gcd, or 1 if one of them is 0 */
	if (a == 0 || b == 0)
		return 1;
	while (b) {
		uint32_t t = a % b;
		a = b;
		b = t;
	}
	return a;
}

static void si5338_calc_MSx(void)
{
	uint64_t c = si5338_MSx_C;
	si5338_MSx_A = (uint16_t)(uint32_t)(si5338_Fvco / c);  /* int() -> dword -> word */
	si5338_MSx_B = (uint32_t)(si5338_Fvco % c);
	uint32_t g = gcd32(si5338_MSx_B, si5338_MSx_C);
	if (si5338_MSx_B == 0) {
		si5338_MSx_C = 1;
	} else if (g > 1) {
		si5338_MSx_B /= g;
		si5338_MSx_C /= g;
	}
}

static void si5338_calc_Px(void)
{
	uint64_t a = si5338_MSx_A, b = si5338_MSx_B, c = si5338_MSx_C;
	si5338_MSx_P1 = (uint32_t)(((a * c + b) * 128) / c - 512);
	si5338_MSx_P2 = (uint32_t)((b * 128) % c);
	si5338_MSx_P3 = si5338_MSx_C;
}

static void put32(uint8_t at, uint32_t v)  /* dword overlay at buffer_bytes(at), little endian */
{
	for (uint8_t i = 0; i < 4; i++)
		buffer_bytes[at + i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get32(uint8_t at)
{
	uint32_t v = 0;
	for (uint8_t i = 0; i < 4; i++)
		v |= (uint32_t)buffer_bytes[at + i] << (8 * i);
	return v;
}

static void si5338_calc_write(void)
{
	i2c_device_addr = si5338_adr;
	i2c_reg = BAS_LOOKUP_B(si5338_calc_msn_reg, si5338_number);
	i2c_size = 10;
	i2c_rbuffer(BUS);
	uint8_t p3_cache = buffer_bytes[1];
	put32(1, 0);  /* si5338_buffer_P1 at buffer_bytes(1) */
	put32(3, 0);  /* si5338_buffer_P2 at buffer_bytes(3) */
	put32(7, 0);  /* si5338_buffer_P3 at buffer_bytes(7) */
	put32(3, si5338_MSx_P2 << 2);
	si5338_MSx_P1 &= 0x3FFFF;
	put32(1, get32(1) | si5338_MSx_P1);
	buffer_bytes[10] = p3_cache;  /* as BASCOM: old bits 7:0 of the top P3 register are ORed */
	si5338_MSx_P3 &= 0x3FFFFFFF;
	put32(7, get32(7) | si5338_MSx_P3);
	for (uint8_t i = 1, c = 10; i <= 5; i++, c--) {
		uint8_t a = buffer_bytes[i];
		buffer_bytes[i] = buffer_bytes[c];
		buffer_bytes[c] = a;
	}
	i2c_device_addr = si5338_adr;
	i2c_reg = BAS_LOOKUP_B(si5338_calc_msn_reg, si5338_number);
	i2c_size = 10;
	i2c_wbuffer(BUS);
}

static void si5338_calc_adjusts(void)
{
	si5338_Fvco = FVCO_DEFAULT;
	uint8_t best_jitter = eram_rb(ERAM_SI5338_OUT_BEST_JITTER_ERAM);
	if (best_jitter >= 1 && best_jitter <= si5338_max_drivers) {
		uint64_t da = eram_rd(ERAM_EL(SI5338_OUT_CLOCK_ERAM, best_jitter));
		if (da && (uint32_t)(si5338_Fvco % da) > 0)  /* PORT: da = 0 would divide by zero */
			si5338_Fvco = (si5338_Fvco / da + 1) * da;
	}
	si5338_number = si5338_max_drivers + 1;  /* MSN: feedback divider */
	int16_t fine = (int16_t)eram_rw(ERAM_SI5338_CUSTOM_MCLK_FINE_TUNE);
	si5338_MSx_C = eram_rd(ERAM_SI5338_MCLK_ERAM) + (uint32_t)(int32_t)fine;
	si5338_calc_MSx();
	si5338_calc_Px();
	si5338_calc_write();
	for (uint8_t i = 1; i <= si5338_max_drivers; i++) {
		si5338_number = i;
		si5338_MSx_C = eram_rd(ERAM_EL(SI5338_OUT_CLOCK_ERAM, i));
		/* PORT: a clock of 0 would divide by zero in the original; leave that output alone */
		if (si5338_MSx_C == 0)
			continue;
		si5338_calc_MSx();
		si5338_calc_Px();
		si5338_calc_write();
	}
}

static void si5338_post_load(void)
{
	uint8_t v;
	do {
		delay_ms(50);
		v = rreg(218) & 0x04;
	} while (v != 0);
	wreg(49, rreg(49) & 0x7F);  /* FCAL_OVRD_EN = 0 */
	wreg(246, 0x02);            /* soft reset */
	wreg(241, 0x65);            /* DIS_LOL = 0 */
	do {
		delay_ms(50);           /* wait for the Si5338 to be ready after calibration */
		v = rreg(218) & 0x15;
	} while (v != 0);
	wreg(45, rreg(235));        /* copy FCAL values */
	wreg(46, rreg(236));
	buffer_bytes[2] = rreg(237) & 0x03;
	buffer_bytes[3] = rreg(47) & 0xFC;
	wreg(47, buffer_bytes[2] | buffer_bytes[3]);
	wreg(49, rreg(49) & 0x80);  /* as BASCOM ("FCAL_OVRD_EN = 1" in the comment) */
	wreg(230, 0x00);            /* OEB_ALL = 0 */
}

void si5338_load(void)
{
	if (!(hardware_other & (1u << BAS_SI5338_RDY)))
		return;
	delay_ms(20);
	si5338_pre_load();
	si5338_load_data();
	si5338_calc_adjusts();
	si5338_post_load();
}

static void si5338_init(void)
{
	if (registers[BAS_REG_STARTUP_STATE] == BAS_STARTUP_STATE_SYSINIT) {
		eram_wd(ERAM_SI5338_MCLK_ERAM, BAS_SI5338_MCLK_INIT);
		eram_ww(ERAM_SI5338_CUSTOM_MCLK_FINE_TUNE, (uint16_t)BAS_SI5338_MCLK_FINE_TUNE_INIT);
		eram_wb(ERAM_SI5338_OUT_BEST_JITTER_ERAM, BAS_SI5338_OUT_BEST_JITTER_INIT);
		eram_wb(ERAM_SI5338_OUT_VOLTAGE_ERAM, BAS_SI5338_OUT_VOLTAGE_INIT);
		bas_reader r;
		for (uint8_t i = 1; i <= si5338_max_drivers; i++) {
			r.p = &si5338_out_clock_init[i * 4];
			eram_wd(ERAM_EL(SI5338_OUT_CLOCK_ERAM, i), bas_read_w(&r) | (uint32_t)bas_read_w(&r) << 16);
		}
		for (uint8_t i = 1; i <= si5338_max_drivers; i++)
			eram_wb(ERAM_EL(SI5338_OUT_SIGNAL_TYPE_ERAM, i), BAS_LOOKUP_B(si5338_out_signal_type_init, i));
		for (uint8_t i = 1; i <= si5338_max_drivers; i++)
			eram_wb(ERAM_EL(SI5338_OUT_DRIVER_TRIM_ERAM, i), BAS_LOOKUP_B(si5338_out_driver_trim_init, i));
		eram_wb(ERAM_SI5338_OUT_DRIVER_ONOFF_ERAM, BAS_SI5338_OUT_DRIVER_ONOFF_INIT);
	}
	si5338_load();
}

void si5338_detect_and_init(void)
{
	if (channel_open & (1u << (4 + BUS))) {
		twi_start(BUS);
		twi_wbyte(BUS, 0xE0);  /* si5338_adr_w */
		if (i2c_err == 0) {
			si5338_adr = 0xE0;
			hardware_other |= 1u << BAS_SI5338_RDY;
		} else {
			twi_stop(BUS);
			delay_ms(5);
			twi_start(BUS);
			twi_wbyte(BUS, 0xE2);  /* si5338_e2_adr_w */
			if (i2c_err == 0) {
				si5338_adr = 0xE2;
				hardware_other |= 1u << BAS_SI5338_RDY;
			} else {
				errorcount((uint8_t)(BAS_SI5338_RDY + BAS_ERRORCOUNT_OFFSET_OTHER));
			}
		}
		twi_stop(BUS);
	}
	si5338_init();
}
#endif
