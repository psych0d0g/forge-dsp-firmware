/*
 * Identity of the device within the build (#31). Index 0 = the identity of the generated
 * flavour (default when nothing is stored). License: CC BY-NC-SA 4.0.
 */
#include <string.h>
#include "identity.h"
#include "eram.h"
#include "flavour.h"

struct ident { const char *model, *board; };

/* groups of variants with identical firmware (docs/single-firmware.md) */
static const struct ident group_96eln[] = {{"96ELN", "4.0"}, {"CORED8", "4.0"}, {"CORED8", "4.1"}};
static const struct ident group_96eln3[] = {{"96ELN3", "4.0"}, {"96ELN3", "4.1"}};
static const struct ident own[] = {{FLAVOUR_MODEL, FLAVOUR_BOARD}};

#define ID_MAGIC_ADDR 0x0F00
#define ID_INDEX_ADDR 0x0F01
#define ID_MAGIC 0xA5

static const struct ident *list(uint8_t *n)
{
	/* only the V40/V41 boards share builds; V30-96ELN also reports PCB "4.0" (#10), so the
	 * MCU generation of the build decides, not the identity string */
	if (BAS_MCU_A3_V40 == 1) {
		if (!strcmp(FLAVOUR_MODEL, "96ELN") || !strcmp(FLAVOUR_MODEL, "CORED8")) {
			*n = sizeof group_96eln / sizeof group_96eln[0];
			return group_96eln;
		}
		if (!strcmp(FLAVOUR_MODEL, "96ELN3")) {
			*n = sizeof group_96eln3 / sizeof group_96eln3[0];
			return group_96eln3;
		}
	}
	*n = 1;
	return own;
}

static const struct ident *current(void)
{
	uint8_t n;
	const struct ident *l = list(&n);
	uint8_t i = 0;
	if (eram_rb(ID_MAGIC_ADDR) == ID_MAGIC) {
		i = eram_rb(ID_INDEX_ADDR);
		if (i >= n)
			i = 0;
	} else {
		/* nothing stored: the flavour of this build (e.g. V40-CORED8 when built for it) */
		for (uint8_t k = 0; k < n; k++)
			if (!strcmp(l[k].model, FLAVOUR_MODEL) && !strcmp(l[k].board, FLAVOUR_BOARD))
				i = k;
	}
	return &l[i];
}

const char *identity_model(void) { return current()->model; }
const char *identity_board(void) { return current()->board; }

uint8_t identity_count(void)
{
	uint8_t n;
	(void)list(&n);
	return n;
}

const char *identity_name(uint8_t i, char buf[16])
{
	uint8_t n;
	const struct ident *l = list(&n);
	if (i >= n)
		i = 0;
	strcpy(buf, l[i].model);
	strcat(buf, "-");
	strcat(buf, l[i].board);
	return buf;
}

uint8_t identity_set(const char *model_pcb)
{
	uint8_t n;
	(void)list(&n);
	char buf[16];
	for (uint8_t i = 0; i < n; i++) {
		if (!strcmp(identity_name(i, buf), model_pcb)) {
			eram_wb(ID_INDEX_ADDR, i);
			eram_wb(ID_MAGIC_ADDR, ID_MAGIC);
			return 1;
		}
	}
	return 0;
}
