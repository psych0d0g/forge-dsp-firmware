"""Python model of the chip45boot3 target (XMEGA build), for tests and dry runs.

Follows bootloader/main.c, host.c, commands.c (commit c5a34fa) including their
quirks: the static receive counter is only reset by STX, a frame with a bad CRC
is answered with "e0", unknown or not compiled-in commands get no answer at
all, an empty write frame erases and writes the page at the current address.
Situations that are memory corruption in the C code raise TargetFault here.
"""

from .protocol import (
    STX, ETX, ESC, IDENTIFIER, crc16,
    CMD_READ_VERSION_BOOTLOADER, CMD_READ_VERSION_FIRMWARE, CMD_START_APPLICATION,
    CMD_SET_ADDRESS, CMD_FLASH_WRITE_DATA, CMD_FLASH_READ_DATA,
    CMD_EEPROM_WRITE_DATA, CMD_EEPROM_READ_DATA,
)

HOST_BUFFER_LENGTH = 131
FW_PATTERN = b"FW_VERSION:"


class TargetFault(Exception):
    """The C target would corrupt memory or hang here."""


def low_periods(c: int) -> int:
    """Number of low phases (falling edges) of one 8N1 character on the line."""
    bits = [0] + [(c >> i) & 1 for i in range(8)] + [1]
    n, prev = 0, 1
    for b in bits:
        if b == 0 and prev == 1:
            n += 1
        prev = b
    return n


class Target:
    ENTER_CMD = b"set.1 system.boot=1\r\n"
    def __init__(self, *, app_size=0x40000, boot_size=0x2000, eeprom_size=0x1000,
                 page_size=512, version=(1, 0), fw_version=True, flash_write=True,
                 flash_read=True, eeprom_write=True, eeprom_read=True,
                 boot_locked=True, flash=None, eeprom=None, in_bootloader=True,
                 key_info=b"FAG-96ELN3-4.1-0000-CCBYNCSA-20241206-MDD"):
        self.app_size = app_size
        self.boot_size = boot_size
        self.eeprom_size = eeprom_size
        self.page_size = page_size
        self.version = version
        self.provide = dict(fw_version=fw_version, flash_write=flash_write,
                            flash_read=flash_read, eeprom_write=eeprom_write,
                            eeprom_read=eeprom_read)
        self.boot_locked = boot_locked
        self.key_info = key_info
        self.flash = bytearray([0xFF]) * (app_size + boot_size)
        self.flash[app_size:app_size + 8] = b"c45b3BL\x00"  # marker in the boot section
        if flash:
            self.flash[:len(flash)] = flash
        self.eeprom = bytearray([0xFF]) * eeprom_size
        if eeprom:
            self.eeprom[:len(eeprom)] = eeprom
        self.boot_write_attempts = 0
        self.e0_ram_overwrites = 0
        self.boot_damaged = False
        self.out = bytearray()
        self.state = "app"
        self.app_rx = bytearray()  # what the application would have received
        if in_bootloader:
            self.reset()

    # --- power / reset --------------------------------------------------
    def reset(self):
        """Reset with RXD high: bootloader starts and waits for autobaud."""
        self.state = "autobaud"
        self.lows = 0
        self.count = 0  # static in hostBufferReceive (.bss)
        self.esc_offset = 0
        self.buffer = bytearray(HOST_BUFFER_LENGTH)
        self.memory_address = 0
        self.offset = 0
        self.flash_buffer = bytearray([0xFF]) * self.page_size
        self.temp_page = bytearray([0xFF]) * self.page_size

    def timeout(self):
        """3 s RTC timeout without autobaud: start the application."""
        if self.state in ("autobaud", "hostinit"):
            self.state = "app"

    # --- line ----------------------------------------------------------
    def rx(self, data: bytes):
        for c in data:
            self._rx(c)

    def _rx(self, c):
        if self.state == "app" or self.state == "exited":
            self.app_rx.append(c)
            # set.1 system.boot: software reset into the bootloader
            if self.app_rx.endswith(self.ENTER_CMD):
                self.reset()
            elif self.app_rx.endswith(b"get.1 system.key.info\r\n") and self.key_info:
                self.out += b' <.1 system.key.info="' + self.key_info + b'"\r\n'

        elif self.state == "autobaud":
            # 17 low periods are measured, the first one is dropped
            self.lows += low_periods(c)
            if self.lows >= 17:
                self.state = "hostinit"
        elif self.state == "hostinit":
            if c == ord("U"):
                self._send_raw(IDENTIFIER)
                self.state = "cmd"
        elif self.state == "cmd":
            self._receive(c)

    def _send_raw(self, b):
        self.out += b

    # host.c hostBufferSend()
    def _send(self, payload: bytes):
        crc = crc16(payload)
        body = bytes(payload) + bytes([crc >> 8, crc & 0xFF])
        self.out.append(STX)
        for c in body:
            if c in (STX, ETX, ESC):
                self.out.append(ESC)
                c |= 0x80
            self.out.append(c)
        self.out.append(ETX)

    # host.c hostBufferReceive()
    def _receive(self, c):
        received = False
        if c == STX:
            self.count = 0
        elif c == ESC:
            self.esc_offset = 0x80
        elif c == ETX:
            received = True
        else:
            if self.count >= HOST_BUFFER_LENGTH:
                raise TargetFault("receive buffer overrun (no STX after a full buffer)")
            self.buffer[self.count] = (c - self.esc_offset) & 0xFF
            self.count += 1
            self.esc_offset = 0
        if received or self.count >= HOST_BUFFER_LENGTH:
            if self.count < 2:
                raise TargetFault("frame shorter than CRC: crcCompute(buffer, -2)")
            n = self.count - 2
            crc = crc16(bytes(self.buffer[:n]))
            if self.buffer[n] == crc >> 8 and self.buffer[n + 1] == crc & 0xFF:
                if n > 0:
                    self._parse(bytes(self.buffer[:n]))
            else:
                # C: hostBufferSend((uint8_t *)"e0", 2) appends the CRC to the
                # literal in RAM -> its NUL and one byte behind it are overwritten
                self.e0_ram_overwrites += 1
                self._send(b"e0")

    # commands.c commandsParseBuffer()
    def _parse(self, buf: bytes):
        cmd = buf[0]
        ans = bytearray([cmd])
        boot_start = self.app_size
        if cmd == CMD_READ_VERSION_BOOTLOADER:
            ans[0] |= 0x80
            ans += bytes(self.version)
            self._send(ans)
        elif cmd == CMD_READ_VERSION_FIRMWARE and self.provide["fw_version"]:
            idx = self.flash.find(FW_PATTERN, 0, boot_start - 1)
            if 0 <= idx < boot_start - len(FW_PATTERN):
                p = idx + len(FW_PATTERN)
                f = self.flash
                ans += bytes([((f[p] - 48) * 10 + (f[p + 1] - 48)) & 0xFF,
                              ((f[p + 3] - 48) * 10 + (f[p + 4] - 48)) & 0xFF])
                ans[0] |= 0x80
            else:
                ans += b"\x00\x00"  # uninitialised stack bytes in C
            self._send(ans)
        elif cmd == CMD_START_APPLICATION:
            ans[0] |= 0x80
            self._send(ans)
            self.state = "exited"
        elif cmd == CMD_SET_ADDRESS:
            if len(buf) < 5:
                raise TargetFault("set address with short frame reads stale buffer")
            self.memory_address = int.from_bytes(buf[1:5], "big")
            self.offset = 0
            if self.memory_address < boot_start:
                ans[0] |= 0x80
            self._send(ans)
        elif cmd == CMD_FLASH_WRITE_DATA and self.provide["flash_write"]:
            data = buf[1:]
            ok = True
            if self.offset + len(data) > self.page_size:
                raise TargetFault("flashBuffer overrun (frames not aligned to SPM_PAGESIZE)")
            # (the C do/while also copies one stale byte for an empty frame; unused)
            self.flash_buffer[self.offset:self.offset + len(data)] = data
            self.offset += len(data)
            if self.memory_address + self.offset > boot_start:
                ok = False
            elif self.offset >= self.page_size or len(data) == 0:
                self._page_fill(self.memory_address, self.flash_buffer, self.offset)
                self._page_write(self.memory_address)
                for i in range(self.offset):
                    if self.flash_buffer[i] != self.flash[self.memory_address + i]:
                        ok = False
                        break
                self.memory_address += self.offset
                self.offset = 0
            if ok:
                ans[0] |= 0x80
            self._send(ans)
        elif cmd == CMD_FLASH_READ_DATA and self.provide["flash_read"]:
            count = min(buf[1], 64)
            ans[0] |= 0x80
            while count:
                count -= 1
                if self.memory_address < boot_start:
                    ans.append(self.flash[self.memory_address])
                    self.memory_address += 1
                else:
                    break
            self._send(ans)
        elif cmd == CMD_EEPROM_WRITE_DATA and self.provide["eeprom_write"]:
            data = buf[1:]
            if self.memory_address + len(data) > self.eeprom_size:
                raise TargetFault("eeprom_write_block beyond E2END (no range check in C)")
            self.eeprom[self.memory_address:self.memory_address + len(data)] = data
            self.memory_address += len(data)
            ans[0] |= 0x80
            self._send(ans)
        elif cmd == CMD_EEPROM_READ_DATA and self.provide["eeprom_read"]:
            count = min(buf[1], 64)
            ans[0] |= 0x80
            while count:
                count -= 1
                if self.memory_address <= self.eeprom_size - 1:
                    ans.append(self.eeprom[self.memory_address])
                    self.memory_address += 1
                else:
                    break
            self._send(ans)
        # else: no answer

    # flash.c with XMEGA NVM semantics
    def _page_fill(self, address, data, length):
        ps = self.page_size
        for i in range(0, length, 2):
            o = (address + i) % ps
            self.temp_page[o] = data[i]
            self.temp_page[(o + 1) % ps] = data[i + 1] if i + 1 < len(data) else 0xFF

    def _page_write(self, address):
        ps = self.page_size
        page = address - address % ps
        if page >= self.app_size:
            self.boot_write_attempts += 1
            if not self.boot_locked:
                self.flash[page:page + ps] = self.temp_page
                self.boot_damaged = True
        else:
            self.flash[page:page + ps] = self.temp_page
        self.temp_page = bytearray([0xFF]) * ps


class SimPort:
    """pyserial-like port connected to a Target."""

    def __init__(self, target: Target):
        self.target = target
        self.written = bytearray()

    def write(self, data):
        self.written += data
        self.target.rx(bytes(data))
        return len(data)

    def read(self, n=1):
        out = bytes(self.target.out[:n])
        del self.target.out[:n]
        return out

    def reset_input_buffer(self):
        self.target.out.clear()

    def close(self):
        pass
