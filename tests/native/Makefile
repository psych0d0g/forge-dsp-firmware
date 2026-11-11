# Native build of the firmware modules per flavour:  make -C tests/native [O=build]
O ?= build
CC ?= gcc
ROOT := ../..
FLAVOURS := $(notdir $(wildcard $(ROOT)/src/flavours/*))
SRC := host_iface.c host_i2c.c \
       $(ROOT)/src/i2c.c $(ROOT)/src/store.c $(ROOT)/src/expander.c $(ROOT)/src/si5338.c \
       $(ROOT)/src/tas3108.c $(ROOT)/src/es90x8pro.c $(ROOT)/src/es9018.c $(ROOT)/src/ct7302.c $(ROOT)/src/cs4385.c $(ROOT)/src/cs5366.c $(ROOT)/src/src43x2_cs8416.c \
       $(ROOT)/src/cmd/cmd_core.c $(ROOT)/src/cmd/cmd_route.c $(ROOT)/src/cmd/cmd_system.c $(ROOT)/src/cmd/cmd_audio.c $(ROOT)/src/cmd/cmd_delay.c $(ROOT)/src/cmd/cmd_preset.c $(ROOT)/src/cmd/cmd_mixer.c $(ROOT)/src/cmd/cmd_biquad.c $(ROOT)/src/cmd/cmd_event.c $(ROOT)/src/events.c $(ROOT)/src/modc.c $(ROOT)/src/cmd/cmd_modc.c $(ROOT)/src/cmd/cmd_extender.c $(ROOT)/src/cmd/cmd_i2c.c $(ROOT)/src/ir.c $(ROOT)/src/sys_init.c $(ROOT)/src/sd_preset.c $(ROOT)/src/identity.c $(ROOT)/src/hwcheck.c host_sd.c $(ROOT)/lib/fatfs/src/ff.c \
       $(ROOT)/src/eram.c $(ROOT)/src/registers.c $(ROOT)/src/biquad.c $(ROOT)/src/dsp.c $(ROOT)/src/api.c
# stub/ first: host versions of board.h (pins are printed instead of set)
CFLAGS := -O1 -g -Wall -Wextra -Werror -Istub -I$(ROOT)/include -I$(ROOT)/lib/fatfs/src -DFF_USE_MKFS=1 -DFF_USE_CHMOD=1 \
          -DFW_VERSION_STR='"9.8.7-test"' -DFW_BUILD_DATE='"20990101"' -fsanitize=address,undefined

all: $(addprefix $(O)/cmd-,$(FLAVOURS))

$(O)/cmd-%: $(SRC) $(wildcard $(ROOT)/include/*.h stub/*.h) $(addprefix $(ROOT)/src/flavours/%/,flavour.h flavour_eram.h flavour_data.h flavour_systeminit.c flavour_systeminit.h)
	mkdir -p $(O)
	$(CC) $(CFLAGS) -I$(ROOT)/src/flavours/$* -o $@ $(SRC) $(ROOT)/src/flavours/$*/flavour_systeminit.c -lm

clean:
	rm -rf $(O)
