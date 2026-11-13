/* native check of the simulator API (tools/sim): boot, commands, output, EEPROM, reset */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
uint8_t *sim_eeprom(void);
int sim_boot(void);
void sim_command(const char *cmd);
void sim_loop(uint16_t ms);
const char *sim_output(void);
int sim_reset_pending(void);

static int fails;
static void expect(const char *cmd, const char *want)
{
	sim_command(cmd);
	const char *out = sim_output();
	if (!strstr(out, want)) { printf("FAIL %s: want %s, got %s\n", cmd, want, out); fails++; }
	else printf("ok   %s -> %.*s\n", cmd, (int)strcspn(out, "\r\n"), out);
}

int main(void)
{
	memset(sim_eeprom(), 0xFF, 4096);
	sim_boot();
	if (sim_reset_pending()) { printf("factory defaults written, boot again\n"); sim_boot(); }
	sim_loop(2000);
	sim_output();
	expect("get.1 system.ping", "pong");
	expect("get.1 system.firmware.port", "9.8.7-test");
	expect("set.1 gain.out.1=200", "gain.out.1");
	expect("get.1 gain.out.1", "=200");
	expect("get.1 nonsense.x", "!.1");
	expect("get.1 preset.loaded", "preset.loaded=");
	expect("get.1 system.temperature", "system.temperature=");
	expect("get.1 system.biquads", "system.biquads=");
	sim_loop(10);
	/* the EEPROM keeps the value over a new boot */
	sim_boot();
	expect("get.1 gain.out.1", "=200");
	sim_command("set.1 system.boot=1");
	sim_output();
	printf("reset pending: %d\n", sim_reset_pending());
	return fails != 0;
}
