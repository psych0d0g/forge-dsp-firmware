# Native build of the firmware modules per flavour:  make -C tests/native [O=build]
O ?= build
CC ?= gcc
ROOT := ../..
FLAVOURS := $(notdir $(wildcard $(ROOT)/src/flavours/*))
SRC := host_iface.c host_i2c.c \
       $(ROOT)/src/i2c.c $(ROOT)/src/store.c $(ROOT)/src/expander.c $(ROOT)/src/si5338.c \
       $(ROOT)/src/tas3108.c $(ROOT)/src/es90x8pro.c $(ROOT)/src/es9018.c $(ROOT)/src/ct7302.c $(ROOT)/src/cs4385.c $(ROOT)/src/cs5366.c $(ROOT)/src/src43x2_cs8416.c \
       $(ROOT)/src/cmd/cmd_core.c $(ROOT)/src/cmd/cmd_route.c $(ROOT)/src/cmd/cmd_system.c \
       $(ROOT)/src/eram.c $(ROOT)/src/registers.c
# stub/ first: host versions of board.h (pins are printed instead of set)
CFLAGS := -O1 -g -Wall -Wextra -Werror -Istub -I$(ROOT)/include \
          -DFW_VERSION_STR='"9.8.7-test"' -DFW_BUILD_DATE='"20990101"' -fsanitize=address,undefined

all: $(addprefix $(O)/cmd-,$(FLAVOURS))

$(O)/cmd-%: $(SRC) $(wildcard $(ROOT)/include/*.h stub/*.h) $(addprefix $(ROOT)/src/flavours/%/,flavour.h flavour_eram.h flavour_data.h flavour_systeminit.c flavour_systeminit.h)
	mkdir -p $(O)
	$(CC) $(CFLAGS) -I$(ROOT)/src/flavours/$* -o $@ $(SRC) $(ROOT)/src/flavours/$*/flavour_systeminit.c

clean:
	rm -rf $(O)
