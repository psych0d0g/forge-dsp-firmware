/*
 * Port of libs/ctrl/events. License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "events.h"
#include "adc.h"
#include "adc_hw.h"
#include "api.h"
#include "bas_data.h"
#include "board.h"
#include "ct7302.h"
#include "eram.h"
#include "flavour_data.h"
#include "i2c.h"
#include "registers.h"
#include "factory.h"

uint8_t events_field_value[BAS_MAX_EVENTS + 1];
uint8_t events_is_changed[BAS_MAX_EVENTS + 1];
uint32_t events_bind_modc[BAS_MAX_EVENTS + 1];
uint8_t modc_is_changed[BAS_MAX_MODC + 1];
static uint8_t events_adda_flags;
static uint8_t last_pins;
static uint8_t overflow_pins(void);
#define events_ad_overflow 0
#define events_da_overflow 1

uint8_t events_init_get(uint8_t nr) { return eram_rb(ERAM_EL(EVENTS_FIELD_INIT_ERAM, nr)); }

void events_init_set(uint8_t nr, uint8_t value)
{
	eram_wb(ERAM_EL(EVENTS_FIELD_INIT_ERAM, nr), value);  /* eram_wb waits for the NVM (waitms eeprom_mcu_write_delay) */
}

void ctrl_events_init(void)
{
	/* at sysinit the defaults events_field_init_values; written to the EEPROM only with
	 * the factory reset enabled (factory.h), otherwise used from RAM */
	uint8_t sysinit = sysinit_active();
	uint8_t write = factory_write();
	for (uint8_t i = 1; i <= BAS_MAX_EVENTS; i++) {
		if (write)
			events_init_set(i, BAS_LOOKUP_B(events_field_init_values, i - 1));
		events_field_value[i] = sysinit ? BAS_LOOKUP_B(events_field_init_values, i - 1) : events_init_get(i);
		events_bind_modc[i] = 0;
	}
	events_adda_flags = 0;  /* events_overflow_ad(_da)_init */
	last_pins = overflow_pins();  /* the pin interrupt only reacts to changes */
}

void event_set_value(uint8_t event, uint8_t value)
{
	if (event < 1 || event > BAS_MAX_EVENTS)  /* PORT: BASCOM has no bound here */
		return;
	if (events_field_value[event] != value) {
		events_field_value[event] = value;
		events_is_changed[event]++;
		for (uint8_t b = 0; b <= 31; b++)
			if (events_bind_modc[event] & (1UL << b))
				modc_is_changed[b + 1] |= 1u << BAS_MODC_BIT_EVENTS;
	}
}

void event_set_integer(uint8_t event, int16_t value) { event_set_value(event, (uint8_t)value); }

/* --- pins (interrupt handlers of the original, polled) --- */

static void set_bit0(uint8_t field, uint8_t v)
{
	if ((events_field_value[field] & 1) != v) {
		events_field_value[field] = (uint8_t)((events_field_value[field] & 0xFE) | v);
		events_is_changed[field]++;
	}
}


static void overflow_edge(uint8_t active, uint8_t flag, uint8_t field)
{
	if (active) {
		events_adda_flags |= 1u << flag;
		PIN_SET(PIN_LED_RED);
		events_field_value[field]++;
		events_field_value[field] <<= 1;
		events_is_changed[field]++;
	} else if (events_adda_flags & (1u << flag)) {
		events_adda_flags &= (uint8_t)~(1u << flag);
		PIN_CLR(PIN_LED_RED);
	}
}

static uint8_t overflow_pins(void)
{
	uint8_t pins = PIN_READ(PIN_PERIPHERAL_AD_OVERFLOW);
#if BAS_EVENTS_OVERFLOW_AD_DA > 0
	pins |= (uint8_t)(PIN_READ(PIN_PERIPHERAL_DA_OVERFLOW) << 1);
#endif
	return pins;
}

void events_poll_pins(void)
{
	/* events_shutdown_int */
	event_set_value(BAS_EVENTS_FIELD_SYSTEM_SHUTDOWN,
	                PIN_READ(PIN_CPU_POWER_CTRL_IN) == 0 ? BAS_EVENTS_STATE_TRUE : BAS_EVENTS_STATE_FALSE);
	/* events_usb_dtr */
	event_set_value(BAS_EVENTS_FIELD_EXT_USB_DTR, PIN_READ(PIN_EXT_USB_DTR));
	/* events_grounds_int0 / int1 */
	set_bit0(BAS_EVENTS_FIELD_GROUND_SWITCH0, PIN_READ(PIN_GROUND_SWITCH_1));
	set_bit0(BAS_EVENTS_FIELD_GROUND_SWITCH1, PIN_READ(PIN_GROUND_SWITCH_2));
	set_bit0(BAS_EVENTS_FIELD_GROUND_SWITCH2, PIN_READ(PIN_GROUND_SWITCH_3));
	set_bit0(BAS_EVENTS_FIELD_GROUND_IMP0, PIN_READ(PIN_QUAD_DECODING_1));
	set_bit0(BAS_EVENTS_FIELD_GROUND_IMP1, PIN_READ(PIN_QUAD_DECODING_2));
	set_bit0(BAS_EVENTS_FIELD_GROUND_IMP2, PIN_READ(PIN_QUAD_DECODING_3));
	/* events_overflow_ad(_da)_int: on a pin change (the pin interrupt) */
	uint8_t pins = overflow_pins();
	if (pins != last_pins) {
		uint8_t changed = pins ^ last_pins;
		last_pins = pins;
		if (changed & 1)
			overflow_edge((pins & 1) == (BAS_EVENTS_OVERFLOW_AD_INV ? 0 : 1), events_ad_overflow,
			              BAS_EVENTS_FIELD_OVERFLOW_AD);
#if BAS_EVENTS_OVERFLOW_AD_DA > 0
		if (changed & 2)
			overflow_edge(((pins >> 1) & 1) == (BAS_EVENTS_OVERFLOW_DA_INV ? 0 : 1), events_da_overflow,
			              BAS_EVENTS_FIELD_OVERFLOW_DA);
#endif
	}
}

static void decay(uint8_t field)
{
	if (events_field_value[field] > 0) {
		events_field_value[field]--;
		events_is_changed[field]++;
	}
}

void events_overflow_mainloop(void)
{
#if BAS_MODUL_CS5366 == 1
	if (events_adda_flags & (1u << events_ad_overflow))
		cs5366_reset_overflow();
#endif
	decay(BAS_EVENTS_FIELD_OVERFLOW_AD);
#if BAS_EVENTS_OVERFLOW_AD_DA > 0
	decay(BAS_EVENTS_FIELD_OVERFLOW_DA);
#endif
}

/* --- SPDIF receivers --- */

static uint8_t events_spdif_ch;

#if BAS_MODUL_CT7302 > 0
void events_spdif_init(void) { events_spdif_ch = 0; }

static void spdif_setevent(uint8_t v)
{
	switch (events_spdif_ch) {
#if BAS_EXT_BT720 > 0
	case 0x00: event_set_value(BAS_EVENTS_FIELD_SPDIF_REC_5, v); break;
#endif
#if BAS_EXT_USBAUDIO > 0
	case 0x01: event_set_value(BAS_EVENTS_FIELD_SPDIF_REC_4, v); break;
#endif
	case 0x02: event_set_value(BAS_EVENTS_FIELD_SPDIF_REC_1, v); break;
	case 0x03: event_set_value(BAS_EVENTS_FIELD_SPDIF_REC_2, v); break;
	case 0x04: event_set_value(BAS_EVENTS_FIELD_SPDIF_REC_3, v); break;
	}
}

/* CT7302 (22) scans the inputs; the active input of (20) is asked there instead */
void events_spdif_mainloop(void)
{
	spdif_setevent(ct7302_22_get_form_state());
	events_spdif_ch++;
	if (events_spdif_ch == ct7302_active_channel) {
		spdif_setevent(ct7302_20_get_form_state());
		events_spdif_ch++;
	}
	if (events_spdif_ch > BAS_EVENTS_SPDIF_MAXCH)
		events_spdif_ch = 0;
	ct7302_22_set_mux(events_spdif_ch);
}
#elif BAS_MODUL_CS8416 > 0
static uint8_t events_spdif_loop;
static uint8_t events_spdif_rec_error[BAS_EVENTS_SPDIF_MAXCH + 1];

void events_spdif_init(void) { events_spdif_ch = 1; }

void events_spdif_mainloop(void)
{
	if (events_spdif_loop > 0)
		events_spdif_loop--;
	if (events_spdif_loop == 0) {
		uint8_t e = cs8416_get_rec_error() & 0x07;
		if (events_spdif_rec_error[events_spdif_ch] != e) {
			events_spdif_rec_error[events_spdif_ch] = e;
			events_spdif_loop = BAS_EVENTS_SPDIF_ADDLOOP;
		}
	}
	if (events_spdif_loop != 0)
		return;
	uint8_t v = 0;
	if (events_spdif_rec_error[events_spdif_ch] == 0x00)
		v = (cs8416_get_form_state() & 0x02) ? 0 : 1;
	switch (events_spdif_ch) {
	case 0x01: event_set_value(BAS_EVENTS_FIELD_SPDIF_REC_3, v); break;
	case 0x02: event_set_value(BAS_EVENTS_FIELD_SPDIF_REC_2, v); break;
#if BAS_EXT_USBAUDIO > 0
	case 0x03: event_set_value(BAS_EVENTS_FIELD_SPDIF_REC_1, v); break;
#endif
	case 0x04: event_set_value(BAS_EVENTS_FIELD_SPDIF_REC_4, v); break;
	}
	events_spdif_ch++;
	if (events_spdif_ch > BAS_EVENTS_SPDIF_MAXCH)
		events_spdif_ch = 1;
	cs8416_set_mux((uint8_t)(events_spdif_ch - 1));
	events_spdif_loop = BAS_EVENTS_SPDIF_MINLOOP;
	cs8416_get_rec_error();  /* read blind error state behind mux change */
}
#else
void events_spdif_init(void) {}
void events_spdif_mainloop(void) {}
#endif

/* --- ADC (libs/ctrl/events/adc) --- */

uint16_t events_adc_values[BAS_EVENTS_ADC_CHANNELS + 1];
uint16_t events_adc_McuMaxTempOOR, events_adc_McuMaxTemp85C, events_adc_McuMaxTemp90C,
         events_adc_McuMaxTemp95C, events_adc_McuMaxTemp100C;
double events_adc_temp_correction;
static uint8_t events_adc_counter;
static uint16_t adc_buf1[BAS_EVENTS_ADC_MAX_COUNT + 1], adc_buf2[BAS_EVENTS_ADC_MAX_COUNT + 1];
#if BAS_FEATURE_MCU_TEMP > 0
static uint16_t adc_buf3[BAS_EVENTS_ADC_MAX_COUNT + 1];
#endif

void events_adc_init(void)
{
#if BAS_FEATURE_MCU_TEMP > 0
	/* the factory reading at 85 °C gives the kelvin per count factor */
	events_adc_McuMaxTemp85C = (uint16_t)(adc_hw_tempsense() - BAS_EVENTS_ADC_DELTAV);
	double corr = (BAS_EVENTS_ADC_KELVIN2CELCIUS + 85.0) / (double)(int32_t)events_adc_McuMaxTemp85C;
	events_adc_temp_correction = corr;
	events_adc_McuMaxTempOOR = (uint16_t)(int32_t)((BAS_EVENTS_ADC_KELVIN2CELCIUS - 40.0) / corr);
	events_adc_McuMaxTemp90C = (uint16_t)(int32_t)((BAS_EVENTS_ADC_KELVIN2CELCIUS + 90.0) / corr);
	events_adc_McuMaxTemp95C = (uint16_t)(int32_t)((BAS_EVENTS_ADC_KELVIN2CELCIUS + 95.0) / corr);
	events_adc_McuMaxTemp100C = (uint16_t)(int32_t)((BAS_EVENTS_ADC_KELVIN2CELCIUS + 100.0) / corr);
#endif
	adc_hw_init();
	events_adc_counter = BAS_EVENTS_ADC_MAX_COUNT;
}

void events_adc_mainloop_32(void)
{
	events_adc_counter--;
	if (events_adc_counter == 0)
		events_adc_counter = BAS_EVENTS_ADC_MAX_COUNT;
	adc_buf1[events_adc_counter] = adc_hw_get(0, 0);
	adc_buf2[events_adc_counter] = adc_hw_get(0, 1);
#if BAS_FEATURE_MCU_TEMP > 0
	adc_buf3[events_adc_counter] = (uint16_t)(adc_hw_get(1, 0) - BAS_EVENTS_ADC_DELTAV);
#endif
}

static uint16_t avg(const uint16_t *b)
{
	uint32_t d = 0;
	for (uint8_t i = 1; i <= BAS_EVENTS_ADC_MAX_COUNT; i++)
		d += b[i];
	return (uint16_t)(d / BAS_EVENTS_ADC_MAX_COUNT);
}

void events_adc_mainloop_16(void)
{
	events_adc_values[1] = avg(adc_buf1);
	events_adc_values[2] = avg(adc_buf2);
#if BAS_FEATURE_MCU_TEMP > 0
	if (adc_buf3[events_adc_counter] > events_adc_values[3])
		events_adc_values[3] = adc_buf3[events_adc_counter];  /* peak */
	events_adc_values[4] = avg(adc_buf3);
#endif
}

/* overheated MCU: mute for a while (auto unmute counter), count an error */
void events_adc_mainloop_41(void)
{
#if BAS_FEATURE_MCU_TEMP > 0
	if (events_adc_values[4] > events_adc_McuMaxTemp85C) {
		api_set_out_mute_change();
		if (api_autounmute_counter < BAS_EVENTS_ADC_OVERHEATED_MUTE) {
			api_autounmute_counter = BAS_EVENTS_ADC_OVERHEATED_MUTE;
			error_count++;
		}
	}
#endif
}
