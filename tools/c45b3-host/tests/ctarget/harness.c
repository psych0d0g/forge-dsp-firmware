/*
 * Native harness around the original chip45boot3 host.c/commands.c/crc.c.
 * Line = stdin/stdout (binary). Replaces main.c, autobaud.c, usart.c, flash.c
 * with host-side equivalents; main loop copied from main.c.
 *
 * usage: harness FLASH_IN|- EEPROM_IN|- FLASH_OUT EEPROM_OUT
 * Memory is dumped on "start application" and on EOF of stdin.
 * stderr: "BOOTPAGE <addr>" for every page write attempt into the boot
 * section (treated as locked, i.e. ignored).
 */
#include <errno.h>
#include <poll.h>
#include <unistd.h>

#include "configure.h"
#include "host.h"
#include "commands.h"
#include "usart.h"
#include "timeout.h"
#include "flash.h"

uint8_t sim_flash[SIM_FLASH_SIZE];
uint8_t sim_eeprom[E2END + 1];
static uint8_t temp_page[SPM_PAGESIZE];
static const char *flash_out, *eeprom_out;
static int eof_seen;

extern const uint8_t ucFirmwarePattern[];

static void load(const char *path, uint8_t *dst, size_t size) {
	memset(dst, 0xFF, size);
	if (strcmp(path, "-") == 0) return;
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); exit(2); }
	size_t n = fread(dst, 1, size, f);
	(void)n;
	fclose(f);
}

static void save(const char *path, const uint8_t *src, size_t size) {
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); exit(2); }
	fwrite(src, 1, size, f);
	fclose(f);
}

static void dump_and_exit(int code) {
	save(flash_out, sim_flash, SIM_FLASH_SIZE);
	save(eeprom_out, sim_eeprom, sizeof sim_eeprom);
	exit(code);
}

/* --- usart.c replacement ------------------------------------------------ */
void usartInit(uint32_t d) { (void)d; }
void usartDeInit(void) {}

void usartPutChar(uint8_t c) {
	if (write(1, &c, 1) != 1) exit(3);
}

uint8_t usartGetChar(uint8_t *c) {
	struct pollfd p = { .fd = 0, .events = POLLIN };
	int r = poll(&p, 1, 20);
	if (r <= 0) return 0;
	ssize_t n = read(0, c, 1);
	if (n == 1) return 1;
	if (n == 0) { eof_seen = 1; dump_and_exit(0); }
	if (errno == EINTR) return 0;
	exit(3);
}

uint8_t timeoutExpired(void) { return eof_seen; }

/* --- flash.c replacement, XMEGA NVM semantics --------------------------- */
void flashPageFill(uint32_t address, uint8_t *buffer, unsigned int length) {
	for (unsigned int i = 0; i < length; i += 2) {
		uint32_t o = (address + i) % SPM_PAGESIZE;
		temp_page[o] = buffer[i];
		temp_page[(o + 1) % SPM_PAGESIZE] = buffer[i + 1];
	}
}

void flashPageWrite(uint32_t address) {
	uint32_t page = address - address % SPM_PAGESIZE;
	if (page >= SIM_APP_SIZE) {
		fprintf(stderr, "BOOTPAGE 0x%05X\n", (unsigned)page);
	} else {
		memcpy(sim_flash + page, temp_page, SPM_PAGESIZE);
	}
	memset(temp_page, 0xFF, SPM_PAGESIZE);
}

/* --- autobaud.c replacement: count low phases like autobaudDo() --------- */
static unsigned low_periods(uint8_t c) {
	unsigned n = 0, prev = 1;
	for (int i = -1; i <= 8; i++) {
		unsigned b = i < 0 ? 0 : (i == 8 ? 1 : (c >> i) & 1);
		if (b == 0 && prev == 1) n++;
		prev = b;
	}
	return n;
}

static uint8_t buffer[HOST_BUFFER_LENGTH];

int main(int argc, char **argv) {
	if (argc != 5) { fprintf(stderr, "usage: %s FLASH_IN EEPROM_IN FLASH_OUT EEPROM_OUT\n", argv[0]); return 2; }
	uintptr_t pat = (uintptr_t)ucFirmwarePattern;
	if (pat < SIM_FLASH_SIZE || pat > 0xFFFFFFFFu) {
		fprintf(stderr, "harness must be linked -no-pie (pattern at %p)\n", (void *)pat);
		return 2;
	}
	load(argv[1], sim_flash, SIM_FLASH_SIZE);
	memcpy(sim_flash + SIM_APP_SIZE, "c45b3BL", 8);
	load(argv[2], sim_eeprom, sizeof sim_eeprom);
	flash_out = argv[3];
	eeprom_out = argv[4];
	memset(temp_page, 0xFF, sizeof temp_page);

	/* autobaudDo(): 17 low periods */
	unsigned lows = 0;
	uint8_t c;
	while (lows < 17) {
		if (usartGetChar(&c)) lows += low_periods(c);
	}

	uint32_t div = 0;
	if (hostInit(&div) != 1) dump_and_exit(1);

	/* main.c loop */
	for (;;) {
		int8_t length = hostBufferReceive(buffer);
		if (length > 0) {
			c = commandsParseBuffer(buffer, length);
			if (c == (CMD_START_APPLICATION | 0x80)) {
				hostDeInit();
				fprintf(stderr, "START_APPLICATION\n");
				dump_and_exit(0);
			}
		}
	}
}
