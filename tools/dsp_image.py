#!/usr/bin/env python3
"""TAS3108 EEPROM boot images (issue #43): parse, check, rebuild.

The DSP programs are stored in the MCU firmware as dsp_N_code (first word = size, then the
image of the AT24C512 the TAS3108 boots from). Format (TAS3108 data manual SLES152B,
section 6, table 6-5): a sequence of blocks

    0  checksum (big endian, low 16 bits of the sum of bytes 2 .. end-2 of the block)
    2  header id 0x001F
    4  memory: 0 = 8051 program (or termination when address = length = 0),
               1 = 8051 external data, 2 = DSP program, 3 = DSP coefficients, 4 = DSP data
    5  0
    6  start address (big endian, in words of that memory)
    8  total bytes of the block (big endian) = 12 + data + trailer
   10  0 0
   12  data: words little endian, 4 bytes (8051, coefficients), 6 bytes (data), 7 bytes
       (program, 54 valid bits)
  end  trailer of one word: zeros, then the checksum again (big endian) in the last 2 bytes

Usage:
    dsp_image.py list IMAGE.bin            blocks with address, words, checksum state
    dsp_image.py extract FLAVOUR_DATA.h N OUT.bin   image of dsp_N_code from a flavour
    dsp_image.py check IMAGE.bin           parse + rebuild bit identical (exit 1 otherwise)
    dsp_image.py regs IMAGE.bin            I2C subaddress table of the 8051 firmware (0x2C00)
License: MIT (tool); the images are CC BY-NC-SA 4.0 (Forge DSP firmware).
"""
import re
import sys

MEM = {0: "8051-prog", 1: "8051-xdata", 2: "dsp-prog", 3: "dsp-coef", 4: "dsp-data"}
WORD = {0: 4, 1: 4, 2: 7, 3: 4, 4: 6}


class Block:
    def __init__(self, mem, addr, data, trailer_len, checksum=None):
        self.mem, self.addr, self.data, self.trailer_len = mem, addr, data, trailer_len
        self.checksum = checksum

    @property
    def words(self):
        return [self.data[i:i + WORD[self.mem]] for i in range(0, len(self.data), WORD[self.mem])]

    def header(self):
        total = 12 + len(self.data) + self.trailer_len if (self.mem or self.addr or self.data) else 0
        return bytes([0x00, 0x1F, self.mem, 0, self.addr >> 8, self.addr & 0xFF, total >> 8, total & 0xFF, 0, 0])

    def computed(self):
        return sum(self.header() + self.data) & 0xFFFF

    def build(self):
        # the termination header of the Forge images carries checksum 0000 (not 001F)
        c = self.checksum if self.terminator and self.checksum is not None else self.computed()
        body = self.header() + self.data
        if self.trailer_len:
            body += bytes(self.trailer_len - 2) + bytes([c >> 8, c & 0xFF])
        return bytes([c >> 8, c & 0xFF]) + body

    @property
    def terminator(self):
        return self.mem == 0 and self.addr == 0 and not self.data


def parse(img):
    """-> (blocks, rest). rest = bytes after the termination header (normally none)."""
    blocks, p = [], 0
    while p + 12 <= len(img):
        cs = img[p] << 8 | img[p + 1]
        if img[p + 2:p + 4] != b"\x00\x1F":
            raise ValueError(f"0x{p:04X}: header id {img[p + 2:p + 4].hex()}, not 001f")
        mem, addr, total = img[p + 4], img[p + 6] << 8 | img[p + 7], img[p + 8] << 8 | img[p + 9]
        if total == 0:
            b = Block(mem, addr, b"", 0, cs)
            blocks.append(b)
            p += 12
            break
        w = WORD.get(mem)
        if w is None:
            raise ValueError(f"0x{p:04X}: memory type {mem}")
        n = total - 12 - w
        if n < 0 or n % w:
            raise ValueError(f"0x{p:04X}: length {total} does not fit words of {w} bytes")
        data = img[p + 12:p + 12 + n]
        tr = img[p + 12 + n:p + total]
        b = Block(mem, addr, data, w, cs)
        b.trailer = tr
        b.offset = p
        blocks.append(b)
        p += total
    return blocks, img[p:]


def build(blocks, rest=b""):
    return b"".join(b.build() for b in blocks) + rest


def check(img):
    blocks, rest = parse(img)
    errors = []
    for b in blocks:
        if b.terminator:
            continue
        c = b.computed()
        if b.checksum != c:
            errors.append(f"block @0x{b.offset:04X}: checksum {b.checksum:04X}, computed {c:04X}")
        if b.trailer[-2:] != bytes([c >> 8, c & 0xFF]) or any(b.trailer[:-2]):
            errors.append(f"block @0x{b.offset:04X}: trailer {b.trailer.hex()}")
    if build(blocks, rest) != img:
        errors.append("rebuild differs")
    return blocks, rest, errors


def c51(blocks):
    """8051 program memory (block addresses are byte addresses) as flat bytes."""
    mem = bytearray()
    for b in blocks:
        if b.mem == 0 and b.data:
            end = b.addr + len(b.data)
            if len(mem) < end:
                mem.extend(bytes(end - len(mem)))
            mem[b.addr:end] = b.data
    return bytes(mem)


REG_TABLE = 0x2C00


def regmap(blocks):
    """I2C subaddress table of the TAS3108 8051 firmware: 256 entries of 4 bytes at 0x2C00,
    [address high, address low, length in bytes, kind]. Kind 1 = DSP coefficient memory
    (address in words, length / 4 words); other kinds are handled by the 8051 code
    (status, delay, ...), address 0. -> list of (subaddress, address, length, kind)."""
    m = c51(blocks)
    if len(m) < REG_TABLE + 1024:
        raise ValueError("no subaddress table at 0x2C00")
    return [(s, m[REG_TABLE + 4 * s] << 8 | m[REG_TABLE + 4 * s + 1], m[REG_TABLE + 4 * s + 2],
             m[REG_TABLE + 4 * s + 3]) for s in range(256)]


def regruns(regs):
    """Group consecutive entries of the same kind and length (coefficients: contiguous addresses)."""
    runs = []
    for s, a, n, k in regs:
        r = runs[-1] if runs else None
        if r and k == r["kind"] and n == r["len"] and s == r["last"] + 1 and (
                a == r["addr"] + (s - r["first"]) * n // 4 if k == 1 else a == r["addr"]):
            r["last"] = s
            continue
        runs.append({"first": s, "last": s, "addr": a, "len": n, "kind": k})
    return runs


def extract(header_path, n):
    t = open(header_path).read()
    m = re.search(r"BAS_DATA_TABLE\(dsp_%d_code, \d+\) = \{(.*?)\};" % n, t, re.S)
    if not m:
        raise SystemExit(f"dsp_{n}_code not in {header_path}")
    b = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))
    size = b[0] | b[1] << 8
    return b[2:2 + size]


def main(argv):
    if len(argv) >= 2 and argv[0] == "list":
        img = open(argv[1], "rb").read()
        blocks, rest, errors = check(img)
        for b in blocks:
            if b.terminator:
                print("end")
                continue
            print(f"@0x{b.offset:04X} {MEM[b.mem]:10s} addr 0x{b.addr:04X} words {len(b.words):5d}"
                  f" bytes {len(b.data):5d} checksum {b.checksum:04X}")
        if rest:
            print(f"{len(rest)} bytes after the end block")
        for e in errors:
            print("ERROR", e)
        return 1 if errors else 0
    if len(argv) == 4 and argv[0] == "extract":
        open(argv[3], "wb").write(extract(argv[1], int(argv[2])))
        return 0
    if len(argv) == 2 and argv[0] == "check":
        _, _, errors = check(open(argv[1], "rb").read())
        for e in errors:
            print("ERROR", e)
        return 1 if errors else 0
    if len(argv) == 2 and argv[0] == "regs":
        blocks, _ = parse(open(argv[1], "rb").read())
        for r in regruns(regmap(blocks)):
            if r["kind"] == 0 and r["addr"] == 0:
                continue  # unused subaddress
            sub = f"0x{r['first']:02X}" + (f"-0x{r['last']:02X}" if r["last"] != r["first"] else "")
            if r["kind"] == 1:
                n = r["last"] - r["first"] + 1
                end = r["addr"] + n * r["len"] // 4 - 1
                print(f"{sub:11s} coef 0x{r['addr']:03X}-0x{end:03X}  {r['len'] // 4} words each")
            else:
                print(f"{sub:11s} kind 0x{r['kind']:02X}  address 0x{r['addr']:04X}  {r['len']} bytes")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
