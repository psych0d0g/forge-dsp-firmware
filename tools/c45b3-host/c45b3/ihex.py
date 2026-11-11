"""Minimal Intel HEX reader/writer (record types 00-05)."""


class HexError(Exception):
    pass


def read_hex(path) -> dict:
    """Return {address: byte} for all data records."""
    mem = {}
    base = 0
    eof = False
    with open(path, "r", encoding="ascii") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            if eof:
                raise HexError(f"line {lineno}: data after EOF record")
            if line[0] != ":":
                raise HexError(f"line {lineno}: missing ':'")
            try:
                rec = bytes.fromhex(line[1:])
            except ValueError as e:
                raise HexError(f"line {lineno}: {e}") from None
            if len(rec) < 5 or len(rec) != rec[0] + 5:
                raise HexError(f"line {lineno}: bad length")
            if sum(rec) & 0xFF:
                raise HexError(f"line {lineno}: bad checksum")
            n, addr, rtype, data = rec[0], (rec[1] << 8) | rec[2], rec[3], rec[4:-1]
            if rtype == 0x00:
                for i, b in enumerate(data):
                    a = base + addr + i
                    if a in mem and mem[a] != b:
                        raise HexError(f"line {lineno}: overlapping data at 0x{a:X}")
                    mem[a] = b
            elif rtype == 0x01:
                eof = True
            elif rtype == 0x02:
                base = ((data[0] << 8) | data[1]) << 4
            elif rtype == 0x04:
                base = ((data[0] << 8) | data[1]) << 16
            elif rtype in (0x03, 0x05):
                pass  # start address, irrelevant for AVR
            else:
                raise HexError(f"line {lineno}: unknown record type {rtype:02X}")
            del n
    if not eof:
        raise HexError("missing EOF record")
    return mem


def to_bin(mem: dict, start: int = 0, fill: int = 0xFF) -> bytes:
    """Contiguous image from `start` to the highest used address."""
    if not mem:
        return b""
    lo, hi = min(mem), max(mem)
    if lo < start:
        raise HexError(f"data below start address 0x{start:X} (0x{lo:X})")
    out = bytearray([fill]) * (hi - start + 1)
    for a, b in mem.items():
        out[a - start] = b
    return bytes(out)


def _record(addr: int, rtype: int, data: bytes) -> str:
    rec = bytes([len(data), (addr >> 8) & 0xFF, addr & 0xFF, rtype]) + data
    return ":" + (rec + bytes([(-sum(rec)) & 0xFF])).hex().upper()


def write_hex(path, data: bytes, start: int = 0, width: int = 16):
    lines = []
    upper = None
    for off in range(0, len(data), width):
        a = start + off
        if a >> 16 != upper:
            upper = a >> 16
            lines.append(_record(0, 0x04, bytes([upper >> 8, upper & 0xFF])))
        chunk = data[off:off + width]
        # do not let a record cross a 64 KiB boundary
        room = 0x10000 - (a & 0xFFFF)
        if len(chunk) > room:
            lines.append(_record(a & 0xFFFF, 0x00, chunk[:room]))
            upper = (a + room) >> 16
            lines.append(_record(0, 0x04, bytes([upper >> 8, upper & 0xFF])))
            lines.append(_record(0, 0x00, chunk[room:]))
        else:
            lines.append(_record(a & 0xFFFF, 0x00, chunk))
    lines.append(":00000001FF")
    with open(path, "w", encoding="ascii", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
