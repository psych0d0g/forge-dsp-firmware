#!/usr/bin/env python3
"""Disassembler for the TAS3108 DSP program (issue #43).

Instruction word (54 bits, stored as 7 bytes little endian in the boot image), TAS3108/TAS3108IA
Audio DSP Instruction Set Reference Guide SLEU067A, table 2 and appendix A:

    b53-b49 ALU1   b48-b42 ALU2   b41-b37 MOP1 + b36-b27 AD1 (data load)
    b26-b24 MOP2 + b23-b14 AD2 (coefficient load)   b13-b10 MOP3 + b9-b0 AD3 (store)

Output syntax as in SLEU067A: ALU1 | ALU2 | MOP1 | MOP2 | MOP3. With branches (JMP, BNC, BOC,
STOP) the AD3 field is the target (PCADDR). MOP3 = 0 with AD3 != 0 is DLYPTR(ptr).

Deviations from SLEU067A, from the Forge programs (see docs/dsp-images.md):
  - ALU2 CLRACC is 100 1101 (0x4D); the guide gives 101 1101, which is also its code for
    ADD(BR,MR,CLP32,DO7). The input/output block at the start of every program has 0x4D where
    the guide's example (table 5) has CLRACC.
  - ALU1 SHL(2) is 01011; the guide gives 00111, the code of SHR(2). SHR(1..4) = 6..9,
    SHL(1..4) = 10..13.

Usage:
    tas3108_dis.py IMAGE.bin [--regs]    disassemble the DSP program of a boot image; --regs
                                         names coefficient addresses after the I2C subaddress
                                         table (dsp_image.py regs)
License: MIT
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsp_image  # noqa: E402

ALU1 = {
    0x00: "NOP", 0x01: "ABS(B)", 0x02: "NEG(B)", 0x03: "NEG(L)", 0x04: "LOG2", 0x05: "ALOG2",
    0x06: "SHR(1)", 0x07: "SHR(2)", 0x08: "SHR(3)", 0x09: "SHR(4)",
    0x0A: "SHL(1)", 0x0B: "SHL(2)", 0x0C: "SHL(3)", 0x0D: "SHL(4)", 0x16: "SHL(20)",
    0x0E: "ABS(L)", 0x0F: "CLRACC", 0x10: "THRU(B)", 0x11: "THRU(L)",
    0x12: "JMP", 0x13: "BOC", 0x14: "BNC", 0x15: "STOP",
}
BRANCH = {0x12, 0x13, 0x14, 0x15}


def _alu2():
    t = {0x00: "NOP", 0x4D: "CLRACC",
         0x20: "COMP(ACC,LR)", 0x18: "COMP(ACC,MR)", 0x10: "COMP(BR,LR)", 0x08: "COMP(BR,MR)",
         0x50: "ADD(ACC,ZERO,NONE,B)", 0x5B: "ADD(BR,ZERO,CLP32,B)",
         0x6A: "ADD(ACC,ZERO,CLP28,B)", 0x68: "ADD(ACC,ZERO,CLP28,MC)", 0x6B: "ADD(ACC,ZERO,CLP28,L)",
         0x58: "ADD(BR,ZERO,NONE,B)", 0x59: "ADD(BR,ZERO,NONE,L)", 0x5A: "ADD(BR,ZERO,NONE,MC)",
         0x57: "ADD(BR,ZERO,NONE,MD)", 0x74: "ADD(ACC,MR,NONE,MDB)"}
    dst = {1: "MD", 2: "B", 3: "L", 4: "MC", 5: "ACC", 7: "DLYI"}
    for base, a, b in ((0x00, "BR", "MR"), (0x08, "BR", "LR"), (0x10, "ACC", "MR"), (0x18, "ACC", "LR")):
        for k, d in dst.items():
            if (a, b, d) != ("BR", "LR", "ACC"):  # not in the guide
                t[base + k] = f"ADD({a},{b},NONE,{d})"
    do = {("BR", "MR"): (0x2D, 0x2E, 0x2F, 0x28, 0x6C, 0x5C, 0x5D, 0x70),
          ("BR", "LR"): (0x31, 0x32, 0x33, 0x34, 0x6D, 0x5E, 0x5F, 0x71),
          ("ACC", "MR"): (0x35, 0x36, 0x37, 0x38, 0x6E, 0x60, 0x61, 0x72),
          ("ACC", "LR"): (0x39, 0x3A, 0x3B, 0x3C, 0x6F, 0x62, 0x63, 0x73)}
    for (a, b), codes in do.items():
        for n, c in enumerate(codes, 1):
            t[c] = f"ADD({a},{b},CLP32,DO{n})"
    return t


ALU2 = _alu2()
MOP1 = {0x01: "LD({},MD)", 0x02: "LD({},B)", 0x03: "LD({},L)", 0x1C: "LD({},BL)", 0x1F: "LD({},MC)",
        0x05: "LDC({},B)", 0x06: "LDC({},L)", 0x14: "LDC({},MD)",
        0x08: "LNC({},B)", 0x09: "LNC({},L)", 0x07: "LNC({},MD)"}
MOP2 = {0x1: "LD({},MC)", 0x6: "LD({},L)", 0x2: "LDC({},MC)", 0x7: "LNC({},MC)"}
MOP3 = {0x1: "ST(MD,DATA,{})", 0x2: "ST(B,DATA,{})", 0x3: "ST(L,DATA,{})", 0x4: "ST(DLYO,DATA,{})",
        0x6: "ST(DI,DATA,{})", 0x7: "ST(MD,COEF,{})", 0x8: "ST(B,COEF,{})", 0x9: "ST(L,COEF,{})"}
COEF_STORE = {0x7, 0x8, 0x9}


def fields(w):
    return (w >> 49 & 0x1F, w >> 42 & 0x7F, w >> 37 & 0x1F, w >> 27 & 0x3FF,
            w >> 24 & 0x7, w >> 14 & 0x3FF, w >> 10 & 0xF, w & 0x3FF)


def coef_names(regs):
    """coefficient address -> 'sNN' or 'sNN.k' after the subaddress table."""
    names = {}
    for s, a, n, kind in regs:
        if kind != 1:
            continue
        for k in range(n // 4):
            names[a + k] = f"s{s:02X}" + (f".{('b0', 'b1', 'b2', 'a1', 'a2')[k]}" if n == 20 else
                                          f".{k}" if n > 4 else "")
    return names


def disasm(w, cnames=None):
    """-> (text, unknown) for one instruction word."""
    a1, a2, m1, d1, m2, d2, m3, d3 = fields(w)
    unknown = []

    def look(table, code, what):
        if code in table:
            return table[code]
        unknown.append(f"{what}={code:#x}")
        return f"{what}?{code:#x}"

    def caddr(a):
        n = cnames.get(a) if cnames else None
        return f"C_{a:03X}" + (f"<{n}>" if n else "")

    parts = [look(ALU1, a1, "ALU1"), look(ALU2, a2, "ALU2")]
    parts.append("NOP" if m1 == 0 and d1 == 0 else look(MOP1, m1, "MOP1").format(f"D_{d1:03X}"))
    parts.append("NOP" if m2 == 0 and d2 == 0 else look(MOP2, m2, "MOP2").format(caddr(d2)))
    if a1 in BRANCH:
        parts.append(f"PCADDR({d3:#05x})" if m3 == 0 else look(MOP3, m3, "MOP3").format(d3))
    elif m3 == 0:
        parts.append(f"DLYPTR({d3})" if d3 else "NOP")
    else:
        parts.append(look(MOP3, m3, "MOP3").format(caddr(d3) if m3 in COEF_STORE else f"D_{d3:03X}"))
    return " | ".join(parts), unknown


def program(blocks):
    words = {}
    for b in blocks:
        if b.mem == 2:
            for i, x in enumerate(b.words):
                words[b.addr + i] = int.from_bytes(x, "little")
    return [words.get(i, 0) for i in range(max(words) + 1)] if words else []


def main(argv):
    if not argv or argv[0].startswith("-"):
        print(__doc__)
        return 2
    blocks, _ = dsp_image.parse(open(argv[0], "rb").read())
    cnames = coef_names(dsp_image.regmap(blocks)) if "--regs" in argv else None
    bad = 0
    for pc, w in enumerate(program(blocks)):
        text, unknown = disasm(w, cnames)
        bad += bool(unknown)
        print(f"{pc:04X}  {w:014X}  {text}")
    if bad:
        print(f"; {bad} instructions with unknown fields", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
