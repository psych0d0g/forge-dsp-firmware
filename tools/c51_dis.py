#!/usr/bin/env python3
"""Minimal 8051 disassembler for the microcontroller program in TAS3108 boot images (issue #43).

Recursive descent from the reset and interrupt vectors, the handlers of the subaddress kinds
(handler table, see handler_table) and optional extra entry points, so tables are not decoded
as code. Only the standard 8051 SFR names are known; the TAS3108 specific
SFRs are documented in a TI guide that is not available, they show as SFR_xx.

Usage:
    c51_dis.py IMAGE.bin [ENTRY ...]     IMAGE: TAS3108 boot image (dsp_image.py extract) or a
                                         flat 8051 binary (*.c51); ENTRY: extra hex addresses
License: MIT
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsp_image  # noqa: E402

SFR = {0x80: "P0", 0x81: "SP", 0x82: "DPL", 0x83: "DPH", 0x87: "PCON", 0x88: "TCON", 0x89: "TMOD",
       0x8A: "TL0", 0x8B: "TL1", 0x8C: "TH0", 0x8D: "TH1", 0x90: "P1", 0x98: "SCON", 0x99: "SBUF",
       0xA0: "P2", 0xA8: "IE", 0xB0: "P3", 0xB8: "IP", 0xC8: "T2CON", 0xCA: "RCAP2L", 0xCB: "RCAP2H",
       0xCC: "TL2", 0xCD: "TH2", 0xD0: "PSW", 0xE0: "ACC", 0xF0: "B"}
BITS = {0xD7: "CY", 0xD6: "AC", 0xD5: "F0", 0xD4: "RS1", 0xD3: "RS0", 0xD2: "OV", 0xD0: "P",
        0xAF: "EA", 0x88: "IT0", 0x89: "IE0", 0x8A: "IT1", 0x8B: "IE1", 0x8C: "TR0", 0x8D: "TF0",
        0x8E: "TR1", 0x8F: "TF1"}
VECTORS = [0x00, 0x03, 0x0B, 0x13, 0x1B, 0x23, 0x2B, 0x33, 0x3B, 0x43, 0x4B, 0x53]


def direct(a):
    return SFR.get(a, f"SFR_{a:02X}") if a >= 0x80 else f"0x{a:02X}"


def bit(b):
    if b in BITS:
        return BITS[b]
    if b < 0x80:
        return f"0x{0x20 + b // 8:02X}.{b % 8}"
    base = b & 0xF8
    return f"{direct(base)}.{b & 7}"


def decode(m, pc):
    """-> (length, text, targets, falls_through)."""
    op = m[pc]
    b1 = m[pc + 1] if pc + 1 < len(m) else 0
    b2 = m[pc + 2] if pc + 2 < len(m) else 0
    rel = lambda off, ln: (pc + ln + (off - 256 if off > 127 else off)) & 0xFFFF  # noqa: E731
    lo = op & 0x0F
    hi = op >> 4
    reg = f"R{op & 7}"
    ri = f"@R{op & 1}"
    if lo == 1:
        t = ((pc + 2) & 0xF800) | (hi >> 1) << 8 | b1
        return (2, f"{'ACALL' if hi & 1 else 'AJMP'} 0x{t:04X}", [t], bool(hi & 1))
    alu = {0x2: "ADD", 0x3: "ADDC", 0x4: "ORL", 0x5: "ANL", 0x6: "XRL", 0x9: "SUBB"}
    if hi in alu and lo >= 4:
        n = alu[hi]
        if lo == 4:
            return (2, f"{n} A,#0x{b1:02X}", [], True)
        if lo == 5:
            return (2, f"{n} A,{direct(b1)}", [], True)
        return (1, f"{n} A,{ri if lo < 8 else reg}", [], True)
    if hi in (4, 5, 6) and lo in (2, 3):
        n = alu[hi]
        return ((2, f"{n} {direct(b1)},A", [], True) if lo == 2 else
                (3, f"{n} {direct(b1)},#0x{b2:02X}", [], True))
    if lo >= 6 or lo == 4 or lo == 5:
        x = ri if 6 <= lo <= 7 else reg if lo >= 8 else None
        table = {
            0x0: ("INC", "A", f"INC {direct(b1)}"), 0x1: ("DEC", "A", f"DEC {direct(b1)}"),
            0xC: ("XCH", None, f"XCH A,{direct(b1)}"), 0xE: ("MOV A", None, f"MOV A,{direct(b1)}"),
            0xF: ("MOV", None, f"MOV {direct(b1)},A"),
        }
        if hi in (0x0, 0x1):
            n = table[hi][0]
            if lo == 4:
                return (1, f"{n} A", [], True)
            if lo == 5:
                return (2, table[hi][2], [], True)
            return (1, f"{n} {x}", [], True)
        if hi == 0x7:
            if lo == 4:
                return (2, f"MOV A,#0x{b1:02X}", [], True)
            if lo == 5:
                return (3, f"MOV {direct(b1)},#0x{b2:02X}", [], True)
            return (2, f"MOV {x},#0x{b1:02X}", [], True)
        if hi == 0x8:
            if lo == 4:
                return (1, "DIV AB", [], True)
            if lo == 5:
                return (3, f"MOV {direct(b2)},{direct(b1)}", [], True)
            return (2, f"MOV {direct(b1)},{x}", [], True)
        if hi == 0xA:
            if lo == 4:
                return (1, "MUL AB", [], True)
            if lo == 5:
                return (1, "DB 0xA5", [], False)
            return (2, f"MOV {x},{direct(b1)}", [], True)
        if hi == 0xB:
            if lo == 4:
                return (3, f"CJNE A,#0x{b1:02X},0x{rel(b2, 3):04X}", [rel(b2, 3)], True)
            if lo == 5:
                return (3, f"CJNE A,{direct(b1)},0x{rel(b2, 3):04X}", [rel(b2, 3)], True)
            return (3, f"CJNE {x},#0x{b1:02X},0x{rel(b2, 3):04X}", [rel(b2, 3)], True)
        if hi == 0xC:
            if lo == 4:
                return (1, "SWAP A", [], True)
            if lo == 5:
                return (2, f"XCH A,{direct(b1)}", [], True)
            return (1, f"XCH A,{x}", [], True)
        if hi == 0xD:
            if lo == 4:
                return (1, "DA A", [], True)
            if lo == 5:
                return (3, f"DJNZ {direct(b1)},0x{rel(b2, 3):04X}", [rel(b2, 3)], True)
            if lo < 8:
                return (1, f"XCHD A,{x}", [], True)
            return (2, f"DJNZ {x},0x{rel(b1, 2):04X}", [rel(b1, 2)], True)
        if hi == 0xE:
            if lo == 4:
                return (1, "CLR A", [], True)
            if lo == 5:
                return (2, f"MOV A,{direct(b1)}", [], True)
            return (1, f"MOV A,{x}", [], True)
        if hi == 0xF:
            if lo == 4:
                return (1, "CPL A", [], True)
            if lo == 5:
                return (2, f"MOV {direct(b1)},A", [], True)
            return (1, f"MOV {x},A", [], True)
    fixed = {
        0x00: (1, "NOP"), 0x03: (1, "RR A"), 0x13: (1, "RRC A"), 0x23: (1, "RL A"), 0x33: (1, "RLC A"),
        0x72: (2, f"ORL C,{bit(b1)}"), 0x82: (2, f"ANL C,{bit(b1)}"), 0xA0: (2, f"ORL C,/{bit(b1)}"),
        0xB0: (2, f"ANL C,/{bit(b1)}"), 0x92: (2, f"MOV {bit(b1)},C"), 0xA2: (2, f"MOV C,{bit(b1)}"),
        0xB2: (2, f"CPL {bit(b1)}"), 0xB3: (1, "CPL C"), 0xC2: (2, f"CLR {bit(b1)}"), 0xC3: (1, "CLR C"),
        0xD2: (2, f"SETB {bit(b1)}"), 0xD3: (1, "SETB C"), 0xC0: (2, f"PUSH {direct(b1)}"),
        0xD0: (2, f"POP {direct(b1)}"), 0x83: (1, "MOVC A,@A+PC"), 0x93: (1, "MOVC A,@A+DPTR"),
        0x90: (3, f"MOV DPTR,#0x{b1:02X}{b2:02X}"), 0xA3: (1, "INC DPTR"), 0xE0: (1, "MOVX A,@DPTR"),
        0xE2: (1, "MOVX A,@R0"), 0xE3: (1, "MOVX A,@R1"), 0xF0: (1, "MOVX @DPTR,A"),
        0xF2: (1, "MOVX @R0,A"), 0xF3: (1, "MOVX @R1,A"),
    }
    if op in fixed:
        ln, text = fixed[op]
        return (ln, text, [], True)
    if op == 0x02:
        t = b1 << 8 | b2
        return (3, f"LJMP 0x{t:04X}", [t], False)
    if op == 0x12:
        t = b1 << 8 | b2
        return (3, f"LCALL 0x{t:04X}", [t], True)
    if op in (0x22, 0x32):
        return (1, "RET" if op == 0x22 else "RETI", [], False)
    if op == 0x73:
        return (1, "JMP @A+DPTR", [], False)
    if op == 0x80:
        return (2, f"SJMP 0x{rel(b1, 2):04X}", [rel(b1, 2)], False)
    cond = {0x40: "JC", 0x50: "JNC", 0x60: "JZ", 0x70: "JNZ"}
    if op in cond:
        return (2, f"{cond[op]} 0x{rel(b1, 2):04X}", [rel(b1, 2)], True)
    cbit = {0x10: "JBC", 0x20: "JB", 0x30: "JNB"}
    if op in cbit:
        return (3, f"{cbit[op]} {bit(b1)},0x{rel(b2, 3):04X}", [rel(b2, 3)], True)
    return (1, f"DB 0x{op:02X}", [], False)


def disassemble(m, entries):
    code, labels, todo = {}, set(entries), list(entries)
    while todo:
        pc = todo.pop()
        while 0 <= pc < len(m) and pc not in code:
            ln, text, targets, cont = decode(m, pc)
            code[pc] = (ln, text)
            for t in targets:
                labels.add(t)
                if t not in code:
                    todo.append(t)
            if not cont:
                break
            pc += ln
    return code, labels


def handler_table(m):
    """Base of the handler table of the subaddress kinds: the dispatch code reads the kind
    (MOV DPTR,#0x2C03 / MOVC) and computes kind * 4 + base (MUL AB, ADD A,#lo, ADDC A,#hi).
    Each entry: read handler, write handler (big endian). -> base or None."""
    pat = bytes([0x90, 0x2C, 0x03, 0x93, 0x75, 0xF0, 0x04, 0xA4, 0x24])
    i = m.find(pat)
    if i < 0:
        return None
    lo = m[i + len(pat)]
    j = m.find(b"\x34", i + len(pat))
    return m[j + 1] << 8 | lo if 0 < j - i < 20 else None


def handlers(m, kinds):
    """kind -> (read handler, write handler)"""
    base = handler_table(m)
    return {k: (m[base + 4 * k] << 8 | m[base + 4 * k + 1], m[base + 4 * k + 2] << 8 | m[base + 4 * k + 3])
            for k in kinds}


def load(path):
    data = open(path, "rb").read()
    if path.endswith(".c51"):
        return data
    return dsp_image.c51(dsp_image.parse(data)[0])


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    m = load(argv[0])
    entries = [v for v in VECTORS if v < len(m) and m[v] != 0] + [int(x, 16) for x in argv[1:]]
    if handler_table(m) is not None:
        kinds = {m[0x2C03 + 4 * s] for s in range(256)}
        for k, (r, w) in sorted(handlers(m, kinds).items()):
            print(f"; kind 0x{k:02X}: read 0x{r:04X}, write 0x{w:04X}")
            entries += [r, w]
    code, labels = disassemble(m, entries)
    pc, gap = 0, None
    while pc < len(m):
        if pc in code:
            if gap is not None:
                print(f"        ; data 0x{gap:04X}-0x{pc - 1:04X}")
                gap = None
            ln, text = code[pc]
            if pc in labels:
                print(f"L{pc:04X}:")
            print(f"  {pc:04X}  {m[pc:pc + ln].hex(' '):9s}  {text}")
            pc += ln
        else:
            if gap is None:
                gap = pc
            pc += 1
    if gap is not None:
        print(f"        ; data 0x{gap:04X}-0x{len(m) - 1:04X}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
