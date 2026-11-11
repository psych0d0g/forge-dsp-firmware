/*
 * Port of libs/cmd/v20/functions.bas: cmd_check4buffer, cmd_inc1receiver, cmd_dec1receiver.
 * Lines from the slave port go up to the master side with the receiver number + 1,
 * requests for other devices go down to the slave port with receiver - 1.
 * License: CC BY-NC-SA 4.0 (derived from the Forge DSP firmware, Mark Duprau).
 */
#include "cmd.h"
#include "iface.h"

#define com_chr_space 0x20
#define com_chr_dot   0x2E

uint8_t buart_channel;
static uint8_t cmd_from_slave_receiver;
static uint8_t cmd_buffer[1 + 127 + 1];  /* 1-based like dim cmd_buffer(127) */

void cmd_init(void)
{
	cmd_from_slave_receiver = iface_ch_master;  /* as BASCOM (a channel index, not a buart) */
}

static void print_buffer(void)
{
	/* print #buart_channel , cmd_buffer_str ;  (string * 126 at cmd_buffer(1)) */
	for (uint8_t i = 1; i <= 126 && cmd_buffer[i]; i++)
		out_chr((char)cmd_buffer[i]);
}

static void cmd_inc1receiver(void)
{
	for (uint8_t count = 2; count <= 8; count++) {
		if (cmd_buffer[count] == com_chr_space)
			break;
		if (cmd_buffer[count] == com_chr_dot) {
			count++;
			cmd_buffer[count]++;
			break;
		}
	}
}

static void cmd_dec1receiver(void)
{
	for (uint8_t count = 2; count <= 8; count++) {
		if (cmd_buffer[count] == com_chr_space)
			break;
		if (cmd_buffer[count] == com_chr_dot) {
			count++;
			cmd_buffer[count]--;
			break;
		}
	}
}

void cmd_check4buffer(void)
{
	cmd_from_slave_receiver = iface_buart_channel[iface_ch_usb];
	if (!iface_hook_ir_master_online() && iface_hook_usb_dtr() == 0)
		cmd_from_slave_receiver = iface_buart_channel[iface_ch_master];

	for (uint8_t count = 1; count <= iface_ch_max; count++) {
		while (iface_todo[count] > 0) {
			iface_hook_busy(1);
			uint8_t size = iface_copy2buffer(count, cmd_buffer);
			if (count == iface_ch_slave) {
				cmd_inc1receiver();
				buart_channel = cmd_from_slave_receiver;
				print_buffer();
			} else {
				buart_channel = iface_buart_channel[count];
				cmd_default_parse(cmd_buffer, size);
				if (cmd_req_chksum_state == cmd_chksum_state_err) {
					cmd_error_chksum();
				} else {
					uint8_t r = REQ_B(cmd_req_receiver);
					if (r == cmd_target_device0) {
						cmd_answer();
					} else if (r == cmd_target_all) {
						cmd_answer();
						buart_channel = iface_buart_channel[iface_ch_slave];
						print_buffer();
					} else {
						cmd_dec1receiver();
						buart_channel = iface_buart_channel[iface_ch_slave];
						print_buffer();
					}
				}
			}
			iface_hook_busy(0);
		}
		iface_hook_watchdog();
	}
}

/* print helpers on the current channel */
void out_chr(char c) { iface_putc(buart_channel, c); }
void out_str(const char *s) { while (*s) out_chr(*s++); }
void out_crlf(void) { out_chr('\r'); out_chr('\n'); }

void out_u16(uint16_t v)
{
	char b[6];
	uint8_t n = 0;
	do {
		b[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (n)
		out_chr(b[--n]);
}

void out_i16(int16_t v)
{
	if (v < 0) {
		out_chr('-');
		out_u16((uint16_t)(-(int32_t)v));
	} else {
		out_u16((uint16_t)v);
	}
}
