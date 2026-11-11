"""chip45boot3 framing: STX/ETX, ESC stuffing, CRC16.

Ported from chip45boot3 (MIT, Erik Lins): target bootloader/host.c, crc.c and
windows_gui/BootloaderTools.cs, CRCtools.cs (commit c5a34fa).
"""

STX = 0x02
ETX = 0x03
ESC = 0x1B

IDENTIFIER = bytes([STX]) + b"c45b3" + bytes([ETX])  # sent without CRC
AUTOBAUD_CHAR = b"U"

CMD_READ_VERSION_BOOTLOADER = 0x11
CMD_READ_VERSION_FIRMWARE = 0x12
CMD_READ_VERSION_EEPROM = 0x13  # defined in commands.h, not implemented in the target
CMD_START_APPLICATION = 0x18
CMD_SET_ADDRESS = 0x21
CMD_FLASH_WRITE_DATA = 0x22
CMD_FLASH_READ_DATA = 0x23
CMD_EEPROM_WRITE_DATA = 0x24
CMD_EEPROM_READ_DATA = 0x25

OK_BIT = 0x80
MAX_DATA = 64  # bytes per read/write frame (target limit)

# Answer the target sends when the CRC of a received frame did not match
TARGET_CRC_ERROR = b"e0"


def crc16(data: bytes) -> int:
    """CRC as computed by crcCompute() in crc.c.

    Poly 0x1021, register start 0x84CF, bit by bit with 16 zero bits appended
    (augmented form). This equals CRC-16/CCITT-FALSE (init 0xFFFF, direct form).
    """
    crc = 0x84CF
    for c in data:
        for i in range(8):
            top = crc & 0x8000
            crc = ((crc << 1) & 0xFFFF) | ((c >> (7 - i)) & 1)
            if top:
                crc ^= 0x1021
    for _ in range(16):
        top = crc & 0x8000
        crc = (crc << 1) & 0xFFFF
        if top:
            crc ^= 0x1021
    return crc


def encode_frame(payload: bytes) -> bytes:
    """payload + CRC (MSB first), stuffed, wrapped in STX ... ETX."""
    crc = crc16(payload)
    body = bytes(payload) + bytes([crc >> 8, crc & 0xFF])
    out = bytearray([STX])
    for c in body:
        if c in (STX, ETX, ESC):
            out.append(ESC)
            c = (c + 0x80) & 0xFF
        out.append(c)
    out.append(ETX)
    return bytes(out)


class CrcError(Exception):
    pass


class FrameDecoder:
    """Stream decoder, same rules as ReceiveFrame() in the GUI.

    STX resets the buffer, ESC marks the next byte as offset by 0x80, ETX ends
    the frame. Bytes before the first STX are kept as well (like the GUI);
    callers flush the input before sending a command.
    """

    def __init__(self):
        self.buf = bytearray()
        self.esc = 0

    def feed(self, c: int):
        """Feed one byte. Returns the payload (CRC stripped) when a frame
        completes, None otherwise. Raises CrcError on a bad CRC."""
        if c == STX:
            self.buf = bytearray()
            self.esc = 0
            return None
        if c == ESC:
            self.esc = 0x80
            return None
        if c == ETX:
            raw = bytes(self.buf)
            self.buf = bytearray()
            self.esc = 0
            if len(raw) < 2:
                raise CrcError(f"frame too short: {raw.hex()}")
            payload, rx = raw[:-2], (raw[-2] << 8) | raw[-1]
            if crc16(payload) != rx:
                raise CrcError(f"crc mismatch: {raw.hex()}")
            return payload
        self.buf.append((c - self.esc) & 0xFF)
        self.esc = 0
        return None
