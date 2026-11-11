# make -C tests/ctarget            (native gcc, Linux; output in tests/ctarget/build)
O ?= build
CC ?= gcc
OBJCOPY ?= objcopy
SRC := harness.c upstream/commands.c upstream/crc.c
DEPS := $(SRC) upstream/host.c $(wildcard stub/*.h stub/avr/*.h upstream/*.h) Makefile
CFLAGS := -O1 -g -Wall -Wno-int-to-pointer-cast -Wno-pointer-sign -Wno-pointer-to-int-cast \
          -Istub -Iupstream -fno-pie
LDFLAGS := -no-pie
FULL := -DPROVIDE_FIRMWARE_VERSION -DPROVIDE_FLASH_WRITE -DPROVIDE_FLASH_READ \
        -DPROVIDE_EEPROM_WRITE -DPROVIDE_EEPROM_READ
# defines of chip45boot3_target_code/Xmega_Example.cproj (no read commands)
EXAMPLE := -DPROVIDE_FIRMWARE_VERSION -DPROVIDE_FLASH_WRITE -DPROVIDE_EEPROM_WRITE

all: $(O)/ctarget-full $(O)/ctarget-example

# host.c appends the CRC to the string literal "e0" (hostBufferSend((uint8_t *)"e0", 2)).
# On AVR literals live in RAM, so make its string section writable like there.
$(O)/host-%.o: upstream/host.c $(DEPS)
	mkdir -p $(O)
	$(CC) $(CFLAGS) -c -o $@ $<
	$(OBJCOPY) --rename-section .rodata.str1.1=.data.avr_literals,alloc,load,data,contents $@

$(O)/ctarget-full: $(O)/host-full.o $(DEPS)
	$(CC) $(CFLAGS) $(FULL) $(LDFLAGS) -o $@ $(SRC) $<

$(O)/ctarget-example: $(O)/host-example.o $(DEPS)
	$(CC) $(CFLAGS) $(EXAMPLE) $(LDFLAGS) -o $@ $(SRC) $<

clean:
	rm -f $(O)/ctarget-full $(O)/ctarget-example $(O)/host-*.o
