# Native build of the protocol code per flavour:  make -C tests/native [O=build]
O ?= build
CC ?= gcc
ROOT := ../..
FLAVOURS := $(notdir $(wildcard $(ROOT)/src/flavours/*))
SRC := host_iface.c $(ROOT)/src/cmd/cmd_core.c $(ROOT)/src/cmd/cmd_route.c $(ROOT)/src/cmd/cmd_system.c $(ROOT)/src/eram.c $(ROOT)/src/registers.c
CFLAGS := -O1 -g -Wall -Wextra -Werror -I$(ROOT)/include \
          -DFW_VERSION_STR='"9.8.7-test"' -DFW_BUILD_DATE='"20990101"' -fsanitize=address,undefined

all: $(addprefix $(O)/cmd-,$(FLAVOURS))

$(O)/cmd-%: $(SRC) $(wildcard $(ROOT)/include/*.h) $(ROOT)/src/flavours/%/flavour.h $(ROOT)/src/flavours/%/flavour_eram.h
	mkdir -p $(O)
	$(CC) $(CFLAGS) -I$(ROOT)/src/flavours/$* -o $@ $(SRC)

clean:
	rm -rf $(O)
