/*
 * Port of libs/cmd/v20/common/subs/cmd_i2c (version 20): direct I2C access for the GUI.
 *   set i2c.B.A.R=v              write register R of device A on bus B (1/2)
 *   get i2c.B.A.R                read one register
 *   get i2c.B.A.buffer=[r,n]     n bytes from register r (BASCOM buffer order)
 *   get i2c.B.A.dump=[r1,r2]     registers r1..r2 one per line
 *   get i2c.B.scan / i2c.B.state
 * EEPROMs (0xA0..0xAF) are refused as in the original.
 * PORT: dump runs to r2 = 255 without wrapping (BASCOM byte loop would not end).
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware).
 */
#include "cmd.h"
#include "i2c.h"

static void hex2(uint8_t b)
{
	static const char h[] = "0123456789ABCDEF";
	out_chr(h[b >> 4]);
	out_chr(h[b & 15]);
}

static void bin8(uint8_t b)
{
	for (int8_t i = 7; i >= 0; i--)
		out_chr((b >> i) & 1 ? '1' : '0');
}

static uint8_t three_numbers(void)
{
	return REQ_B(6) == cmd_req_number && REQ_B(8) == cmd_req_number && REQ_B(10) == cmd_req_number;
}

static uint8_t is_eeprom(uint8_t a) { return a >= 0xA0 && a <= 0xAF; }

void cmd_set_i2c(void)
{
	if (!three_numbers()) {
		cmd_error_unknown();
		return;
	}
	i2c_device_addr = REQ_B(7);
	if (is_eeprom(i2c_device_addr) || (REQ_B(5) != 1 && REQ_B(5) != 2)) {
		cmd_error_cmd_oor();
		return;
	}
	i2c_reg = REQ_B(9);
	buffer_byte = (uint8_t)cmd_reg_var_integers[1];
	i2c_wbyte(REQ_B(5));
	cmd_answer_ok();
}

static uint8_t device(void)
{
	if (REQ_B(8) != cmd_req_number || is_eeprom(REQ_B(7)) || REQ_B(7) == 0) {
		cmd_error_cmd_oor();
		return 0;
	}
	i2c_device_addr = REQ_B(7);
	return 1;
}

static void get_buffer(void)
{
	if (!device())
		return;
	uint8_t a = (uint8_t)cmd_reg_var_integers[1], b = (uint8_t)cmd_reg_var_integers[2];
	if (REQ_B(6) != cmd_req_number)
		return;  /* as the original: no answer */
	if (REQ_B(5) != 1 && REQ_B(5) != 2) {
		cmd_error_cmd_oor();
		return;
	}
	if (b > BAS_BUFFER_BYTES_MAX)  /* PORT: the original reads beyond buffer_bytes */
		b = BAS_BUFFER_BYTES_MAX;
	i2c_reg = a;
	i2c_size = b;
	i2c_rbuffer(REQ_B(5));
	cmd_answer_header();
	for (uint8_t i = 1; i <= b; i++)
		hex2(buffer_bytes[i]);
	out_crlf();
}

static void get_dump(void)
{
	if (!device())
		return;
	uint8_t a = (uint8_t)cmd_reg_var_integers[1], b = (uint8_t)cmd_reg_var_integers[2];
	if (b == 0)
		b = a;
	if (REQ_B(6) != cmd_req_number)
		return;
	if (REQ_B(5) != 1 && REQ_B(5) != 2) {
		cmd_error_cmd_oor();
		return;
	}
	for (uint16_t r = a; r <= b; r++) {
		i2c_reg = (uint8_t)r;
		i2c_rbyte(REQ_B(5));
		cmd_answer_header();
		out_str("&H");
		hex2((uint8_t)r);
		out_str(";&H");
		hex2(buffer_byte);
		out_str(",&B");
		bin8(buffer_byte);
		out_crlf();
	}
}

static void get_register(void)
{
	if (!three_numbers()) {
		cmd_error_unknown();
		return;
	}
	i2c_device_addr = REQ_B(7);
	if (is_eeprom(i2c_device_addr)) {
		cmd_error_cmd_oor();
		return;
	}
	i2c_reg = REQ_B(9);
	if (REQ_B(5) != 1 && REQ_B(5) != 2) {
		cmd_error_cmd_oor();  /* the original also prints the stale buffer_byte after this */
		return;
	}
	i2c_rbyte(REQ_B(5));
	cmd_answer_header();
	out_str("&H");
	hex2(buffer_byte);
	out_str(",&B");
	bin8(buffer_byte);
	out_crlf();
}

static void get_scan(void)
{
	if (REQ_B(6) != cmd_req_number) {
		cmd_error_unknown();
		return;
	}
	uint8_t bus = REQ_B(5);
	if (bus != 1 && bus != 2) {
		cmd_error_cmd_oor();
		return;
	}
	if (!(channel_open & (1u << (4 + bus))))
		return;
	uint8_t c = 0;
	cmd_answer_header();
	out_chr('[');
	for (uint16_t i = 2; i <= 254; i += 2) {
		twi_start(bus);
		twi_wbyte(bus, (uint8_t)i);
		if (i2c_err == 0) {
			if (c > 0)
				out_chr(',');
			out_str("&H");
			hex2((uint8_t)i);
			c++;
		}
		twi_stop(bus);
		i2c_err = 0;
	}
	out_chr(']');
	out_crlf();
}

static void get_state(void)
{
	if (REQ_B(6) != cmd_req_number)
		return;
	uint8_t bus = REQ_B(5);
	if (bus != 1 && bus != 2) {
		cmd_error_cmd_oor();
		return;
	}
	uint8_t st = twi_master_status(bus);
	cmd_answer_header();
	out_u16(twi_scl_out(bus));
	out_str(", ");
	out_u16(twi_sda(bus));
	out_str(", &H");
	hex2(st);
	out_str(", &B");
	bin8(st);
	out_crlf();
}

void cmd_get_i2c(void)
{
	switch (req_w(cmd_req_word_three)) {
	case 0x7363: get_scan(); return;
	case 0x7374: get_state(); return;
	}
	switch (req_w(cmd_req_word_four)) {
	case 0x6275: get_buffer(); break;
	case 0x6475: get_dump(); break;
	default: get_register();
	}
}
