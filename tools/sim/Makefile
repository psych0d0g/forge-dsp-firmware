# Simulator of the Forge for the web GUI (forge-dsp-gui-ng #11): the native host build of the tests
# (tests/native) with -DSIM, as WebAssembly. One ES module per flavour with the wasm inside:
#   make -C tools/sim [O=build] [FLAVOURS="V40-96ELN V30-96LN"]      (needs emcc, emsdk 6.0.13)
# API (host_iface.c, #ifdef SIM): sim_eeprom() -> 4096 bytes, sim_at24() -> 65536 bytes (presets), sim_boot(), sim_command(line),
# sim_loop(ms), sim_output() -> USB text since the last call, sim_reset_pending().
O ?= build
EMCC ?= emcc
ROOT := ../..
N := $(ROOT)/tests/native
FLAVOURS ?= $(notdir $(wildcard $(ROOT)/src/flavours/*))
NSRC := $(shell $(MAKE) -s -C $(N) print-src)
# paths there are relative to tests/native: ../../x stays (same depth), host files get the directory
SRC := $(filter ../../%,$(NSRC)) $(addprefix $(N)/,$(filter-out ../../%,$(NSRC)))
CFLAGS := -O2 -I$(N)/stub -I$(ROOT)/include -I$(ROOT)/lib/fatfs/src -DFF_USE_MKFS=1 -DFF_USE_CHMOD=1 -DFORGE_DSP_OPT=1 \
          -DLOG_MAX_BYTES=240UL -DSIM -DFORGE_BRINGUP=1 -DFORGE_AUDIO=1 -DFORGE_FACTORY_RESET=1 -DFW_VERSION_STR='"$(shell git -C $(ROOT) describe --tags --always 2>/dev/null | sed 's/^v//')-sim"' \
          -DFW_BUILD_DATE='"$(shell date -u +%Y%m%d)"'
EMFLAGS := -sMODULARIZE=1 -sEXPORT_ES6=1 -sSINGLE_FILE=1 -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1 \
           -sEXPORTED_FUNCTIONS=_sim_eeprom,_sim_at24,_sim_boot,_sim_command,_sim_loop,_sim_output,_sim_reset_pending,_malloc,_free \
           -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,HEAPU8

all: $(addprefix $(O)/forge-sim-,$(addsuffix .mjs,$(FLAVOURS)))

$(O)/forge-sim-%.mjs: $(wildcard $(ROOT)/src/*.c $(ROOT)/src/cmd/*.c $(ROOT)/include/*.h $(N)/*.c) $(ROOT)/src/flavours/%/flavour.h
	mkdir -p $(O)
	$(EMCC) $(CFLAGS) -I$(ROOT)/src/flavours/$* $(EMFLAGS) -o $@ $(SRC) $(ROOT)/src/loopcall.c $(ROOT)/src/flavours/$*/flavour_systeminit.c

clean:
	rm -rf $(O)
