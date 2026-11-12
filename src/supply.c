/*
 * Supply voltage monitoring (port extension, issue #46), see include/supply.h.
 * VCC: ADCB CH1 = VCC/10 against the internal 1 V reference (unsigned mode, offset
 * BAS_EVENTS_ADC_DELTAV as for the temperature). External supply: the averaged ADCA value of
 * a GVOL input (events_adc_values[1/2]) through a divider, two-point calibration.
 * EEPROM 0x0F03..0x0F0E (identity uses 0x0F00/0x0F01, clickless 0x0F02).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "supply.h"
#include "adc_hw.h"
#include "api.h"
#include "cmd.h"
#include "eram.h"
#include "events.h"
#include "flavour.h"

#define EE_SOURCE 0x0F03  /* byte: 0/0xFF off, 1, 2 */
#define EE_CAL    0x0F04  /* 4 words: raw1, mV1, raw2, mV2 */
#define EE_LOW    0x0F0C  /* word: mV, 0/0xFFFF off */
#define EE_MUTE   0x0F0E  /* byte: 1 on */

static uint16_t vcc_buf[BAS_EVENTS_ADC_MAX_COUNT];
static uint8_t vcc_i, vcc_n;
static uint16_t vcc_avg;
static uint8_t low, debounce;

void supply_init(void)
{
	vcc_i = vcc_n = 0;
	vcc_avg = 0;
	low = debounce = 0;
}

void supply_sample(void)
{
	vcc_buf[vcc_i] = adc_hw_get(1, 1);
	vcc_i = (uint8_t)((vcc_i + 1) % BAS_EVENTS_ADC_MAX_COUNT);
	if (vcc_n < BAS_EVENTS_ADC_MAX_COUNT)
		vcc_n++;
}

uint16_t supply_vcc_raw(void) { return vcc_avg; }

uint16_t supply_vcc_mv(void)
{
	if (vcc_avg <= BAS_EVENTS_ADC_DELTAV)
		return 0;
	return (uint16_t)((uint32_t)(vcc_avg - BAS_EVENTS_ADC_DELTAV) * 10000u / 4095u);
}

uint16_t supply_gvol_raw(uint8_t n)
{
	return n == 1 || n == 2 ? events_adc_values[n] : 0;
}

uint8_t supply_source(void)
{
	uint8_t s = eram_rb(EE_SOURCE);
	return s == 1 || s == 2 ? s : 0;
}

void supply_set_source(uint8_t s) { eram_wb(EE_SOURCE, s == 1 || s == 2 ? s : 0xFF); }

void supply_get_cal(int16_t c[4])
{
	for (uint8_t i = 0; i < 4; i++)
		c[i] = (int16_t)eram_rw((uint16_t)(EE_CAL + 2 * i));
}

uint8_t supply_set_cal(const int16_t c[4])
{
	if (c[0] == c[2] || c[0] < 0 || c[2] < 0)
		return 0;
	for (uint8_t i = 0; i < 4; i++)
		eram_ww((uint16_t)(EE_CAL + 2 * i), (uint16_t)c[i]);
	return 1;
}

int16_t supply_ext_mv(void)
{
	uint8_t s = supply_source();
	if (!s)
		return -1;
	int16_t c[4];
	supply_get_cal(c);
	if (c[0] == c[2] || c[0] == -1 || c[2] == -1)
		return -1;  /* not calibrated (erased EEPROM reads 0xFFFF) */
	int32_t raw = (int16_t)supply_gvol_raw(s);  /* ADCA is signed */
	int32_t mv = c[1] + (raw - c[0]) * ((int32_t)c[3] - c[1]) / ((int32_t)c[2] - c[0]);
	if (mv < 0)
		mv = 0;
	if (mv > 32767)
		mv = 32767;
	return (int16_t)mv;
}

uint16_t supply_low_mv(void)
{
	uint16_t v = eram_rw(EE_LOW);
	return v == 0xFFFF ? 0 : v;
}

void supply_set_low_mv(uint16_t mv) { eram_ww(EE_LOW, mv ? mv : 0xFFFF); }

uint8_t supply_mute(void) { return eram_rb(EE_MUTE) == 1; }

void supply_set_mute(uint8_t on) { eram_wb(EE_MUTE, on ? 1 : 0xFF); }

uint8_t supply_low(void) { return low; }

static void send_event(const char *v)
{
	static const uint16_t ev[cmd_req_words_max] = {0x0104, 0x7379, 0x7375, 0x0000, 0x0000};
	uint8_t i = 0;
	for (; v[i]; i++)
		cmd_req_value[i] = v[i];
	cmd_req_value[i] = 0;
	cmd_load_header(ev);
	cmd_sendevent();
}

void supply_mainloop(void)
{
	if (vcc_n) {
		uint32_t d = 0;
		for (uint8_t i = 0; i < vcc_n; i++)
			d += vcc_buf[i];
		vcc_avg = (uint16_t)(d / vcc_n);
	}
	uint16_t thr = supply_low_mv();
	int16_t mv = supply_ext_mv();
	if (!thr || mv < 0) {
		low = debounce = 0;
		return;
	}
	uint8_t want = low ? (uint16_t)mv < thr + SUPPLY_HYST_MV : (uint16_t)mv < thr;
	if (want != low) {
		if (++debounce >= SUPPLY_DEBOUNCE) {
			low = want;
			debounce = 0;
			send_event(low ? "low" : "ok");
		}
	} else {
		debounce = 0;
	}
	if (low && supply_mute()) {
		/* as the overheated MCU: mute, unmute automatically after the counter ran out */
		api_set_out_mute_change();
		if (api_autounmute_counter < BAS_EVENTS_ADC_OVERHEATED_MUTE)
			api_autounmute_counter = BAS_EVENTS_ADC_OVERHEATED_MUTE;
	}
}
