"""chip45boot3 host side (Linux port of BootloaderTools.cs).

`port` is anything pyserial-like: write(bytes), read(n) returning what arrived
within a short timeout (possibly b""), reset_input_buffer().
"""

import time

from .protocol import (
    IDENTIFIER, AUTOBAUD_CHAR, OK_BIT, MAX_DATA, TARGET_CRC_ERROR,
    CMD_READ_VERSION_BOOTLOADER, CMD_READ_VERSION_FIRMWARE, CMD_START_APPLICATION,
    CMD_SET_ADDRESS, CMD_FLASH_WRITE_DATA, CMD_FLASH_READ_DATA,
    CMD_EEPROM_WRITE_DATA, CMD_EEPROM_READ_DATA,
    CrcError, FrameDecoder, encode_frame,
)

# ATxmega256A3U (Forge 96ELN-3)
APP_SIZE = 0x40000       # BOOT_SECTION_START
EEPROM_SIZE = 0x1000     # E2END + 1
PAGE_SIZE = 512          # SPM_PAGESIZE

AUTOBAUD_REPEAT = 16     # like the GUI
CRC_RETRIES = 3


class BootloaderError(Exception):
    pass


class NoAnswer(BootloaderError):
    """Timeout: target did not answer (also: command not compiled in)."""


class Refused(BootloaderError):
    """Target echoed the command without the OK bit."""


class TargetCrcError(BootloaderError):
    """Target reported a CRC error on our frame ("e0")."""


class NotSupported(BootloaderError):
    pass


class Bootloader:
    def __init__(self, port, timeout=3.0, slow_timeout=5.0, log=None, clock=time.monotonic,
                 sleep=time.sleep):
        self.port = port
        self.timeout = timeout
        self.slow_timeout = slow_timeout  # 0x12 scans the whole application flash
        self.log = log or (lambda msg: None)
        self.clock = clock
        self.sleep = sleep
        self.connected = False
        self.target_crc_errors = 0

    # --- connection -----------------------------------------------------
    def connect(self, attempts=10, wait=None, attempt_time=0.3):
        """Send autobaud bursts until the identifier STX c45b3 ETX comes back.

        attempts: number of bursts (GUI: 10 with 3 s each).
        wait: alternatively keep trying for this many seconds, e.g. while the
              target is being power-cycled (bootloader listens ~3 s after reset).
        """
        deadline = self.clock() + wait if wait is not None else None
        n = 0
        seen = bytearray()
        while True:
            n += 1
            self.port.write(AUTOBAUD_CHAR * AUTOBAUD_REPEAT)
            t_end = self.clock() + attempt_time
            while self.clock() < t_end:
                chunk = self.port.read(64)
                if chunk:
                    seen += chunk
                    if IDENTIFIER in seen:
                        self.log(f"bootloader answered after {n} burst(s)")
                        self.sleep(0.05)
                        self.port.reset_input_buffer()
                        self.connected = True
                        return True
                    del seen[:-len(IDENTIFIER)]
                else:
                    self.sleep(0.005)
            if deadline is not None:
                if self.clock() >= deadline:
                    break
            elif n >= attempts:
                break
        self.log(f"no bootloader identifier after {n} burst(s)")
        return False

    def resume(self):
        """Bootloader already in command mode (earlier session ended without
        'start application'): it no longer answers autobaud, probe with 0x11."""
        try:
            self.bootloader_version()
        except (NoAnswer, CrcError, TargetCrcError):
            return False
        self.connected = True
        return True

    # --- frames ---------------------------------------------------------
    def _read_frame(self, timeout):
        dec = FrameDecoder()
        t_end = self.clock() + timeout
        while self.clock() < t_end:
            chunk = self.port.read(1)
            if not chunk:
                self.sleep(0.001)
                continue
            payload = dec.feed(chunk[0])
            if payload is not None:
                return payload
        raise NoAnswer("timeout")

    def transact(self, payload: bytes, timeout=None) -> bytes:
        """Send one frame, return the answer payload.

        Raises NoAnswer, CrcError (answer damaged), TargetCrcError,
        Refused (command echoed without OK bit).
        """
        cmd = payload[0]
        self.port.reset_input_buffer()
        self.port.write(encode_frame(payload))
        ans = self._read_frame(self.timeout if timeout is None else timeout)
        if ans == TARGET_CRC_ERROR:
            # host.c appends the CRC to the literal "e0": one RAM byte behind it
            # is overwritten on the target (content depends on the build)
            self.target_crc_errors += 1
            self.log("WARNING: target reported a CRC error; per host.c this overwrites one "
                     "RAM byte in the bootloader. Finish reading, then power-cycle the target.")
            raise TargetCrcError("target reported CRC error")
        if not ans:
            raise BootloaderError("empty answer")
        if ans[0] == cmd | OK_BIT:
            return ans
        if ans[0] == cmd:
            raise Refused(f"command 0x{cmd:02X} refused: {ans.hex()}")
        raise BootloaderError(f"unexpected answer to 0x{cmd:02X}: {ans.hex()}")

    def _retry(self, payload, timeout=None):
        """For idempotent commands only."""
        for i in range(CRC_RETRIES):
            try:
                return self.transact(payload, timeout)
            except (CrcError, TargetCrcError) as e:
                self.log(f"retry {i + 1}: {e}")
                last = e
        raise last

    # --- commands -------------------------------------------------------
    def bootloader_version(self):
        a = self._retry(bytes([CMD_READ_VERSION_BOOTLOADER]))
        return a[1], a[2]

    def firmware_version(self):
        """(major, minor) from the "FW_VERSION:xx.yy" string in the application.

        None if the bootloader found no such string. NotSupported if the
        command is not compiled in (no answer). The target scans the whole
        application flash, so allow a longer timeout.
        """
        try:
            a = self._retry(bytes([CMD_READ_VERSION_FIRMWARE]), timeout=self.slow_timeout)
        except Refused:
            return None
        except NoAnswer:
            raise NotSupported("0x12 not answered (PROVIDE_FIRMWARE_VERSION missing?)") from None
        return a[1], a[2]

    def set_address(self, address: int):
        self._retry(bytes([CMD_SET_ADDRESS]) + address.to_bytes(4, "big"))

    def start_application(self):
        self.transact(bytes([CMD_START_APPLICATION]))
        self.connected = False

    def _read_memory(self, cmd, start, size, progress=None):
        out = bytearray()
        addr = start
        self.set_address(addr)
        failures = 0
        while size is None or len(out) < size:
            n = MAX_DATA if size is None else min(MAX_DATA, size - len(out))
            try:
                a = self.transact(bytes([cmd, n]))
            except (CrcError, TargetCrcError, NoAnswer) as e:
                if isinstance(e, NoAnswer) and not out and failures == 0:
                    raise NotSupported(
                        f"0x{cmd:02X} not answered (read not compiled in?)") from None
                failures += 1
                if failures > CRC_RETRIES:
                    raise
                self.log(f"read retry at 0x{addr:X}: {e}")
                self.set_address(addr)  # read auto-increments, so re-sync
                continue
            failures = 0
            data = a[1:]
            if not data:
                break  # end of memory reached
            out += data
            addr += len(data)
            if progress:
                progress(len(out))
        return bytes(out)

    def read_flash(self, start=0, size=None, progress=None):
        return self._read_memory(CMD_FLASH_READ_DATA, start, size, progress)

    def read_eeprom(self, start=0, size=None, progress=None):
        return self._read_memory(CMD_EEPROM_READ_DATA, start, size, progress)

    def write_flash(self, data: bytes, start=0, page_size=PAGE_SIZE, app_size=APP_SIZE,
                    progress=None):
        """Program application flash. Target verifies every page.

        Pads to whole pages with 0xFF and never sends the GUI's final empty
        frame: on the target that erases and writes the page *after* the data,
        and for an image that ends at BOOT_SECTION_START this is the first
        bootloader page.
        """
        if start % page_size:
            raise ValueError("start must be page aligned")
        data = bytes(data)
        if len(data) % page_size:
            data += b"\xFF" * (page_size - len(data) % page_size)
        if start + len(data) > app_size:
            raise ValueError(f"image ends at 0x{start + len(data):X}, beyond application section")
        if page_size % MAX_DATA:
            raise ValueError("page size must be a multiple of 64")
        self.set_address(start)
        for off in range(0, len(data), MAX_DATA):
            # no retry: a lost answer leaves the target state unknown
            self.transact(bytes([CMD_FLASH_WRITE_DATA]) + data[off:off + MAX_DATA])
            if progress:
                progress(off + MAX_DATA)
        return len(data)

    def write_eeprom(self, data: bytes, start=0, eeprom_size=EEPROM_SIZE, progress=None):
        if start + len(data) > eeprom_size:
            raise ValueError("data beyond EEPROM end (target does not check)")
        self.set_address(start)
        for off in range(0, len(data), MAX_DATA):
            self.transact(bytes([CMD_EEPROM_WRITE_DATA]) + data[off:off + MAX_DATA])
            if progress:
                progress(off + MAX_DATA)
        return len(data)
