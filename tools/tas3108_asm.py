#!/usr/bin/env python3
"""Assembler and checker for TAS3108 DSP programs (issue #47).

Input: one instruction per line in the syntax of tas3108_dis.py (and SLEU067A),
    ALU1 | ALU2 | MOP1 | MOP2 | MOP3
optionally prefixed by "PC  HEXWORD  " as the disassembler prints it. Also accepted:
    label:                 defines a label (PCADDR(label) refers to it)
    ; comment              ignored (also after an instruction)
    C_013<s1F.b2>          names in <> are ignored; addresses are D_xxx, C_xxx or numbers
    ALU2?0x4f              a raw field value (fields the tables do not name)

Usage:
    tas3108_asm.py asm IN.asm OUT.bin [--image ORIG.bin]
                              assemble; with --image the program block of ORIG is replaced and
                              a complete boot image (blocks, checksums) is written
    tas3108_asm.py check IMAGE.bin [--fs 96000]
                              check the hardware rules of the program in a boot image
License: MIT
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsp_image  # noqa: E402
import tas3108_dis as dis  # noqa: E402

# DSP cycles per sample (SLES152B table 5-4, fDSPCLK 135.2 MHz)
CYCLES = {32000: 4224, 44100: 2816, 48000: 2816, 88200: 1408, 96000: 1408, 192000: 704}
MEM_WORDS = 1022     # data and coefficient memory
JUMP_LIMIT = 0x400   # TAS3108: branch instructions within the first 1K (TAS3xxx guide, JMP note 3)
BRANCH_NOPS = 3      # NOPs after JMP/BOC/BNC
DLY_DISTANCE = 14    # ST(DLYO) at least 14 instructions after DLYPTR (13 cycles between)
COMP_LATENCY = 2     # COMP result usable two cycles later


class AsmError(ValueError):
    pass


def _inverse(table):
    return {v: k for k, v in table.items()}


ALU1_INV = _inverse(dis.ALU1)
ALU2_INV = _inverse(dis.ALU2)


def _templates(table):
    """MOP name templates ('LD({},MD)') -> compiled regex and code."""
    out = []
    for code, t in table.items():
        rx = re.escape(t).replace(r"\{\}", r"(.+)")
        out.append((re.compile(rx + r"$"), code))
    return out


MOP1_T, MOP2_T, MOP3_T = _templates(dis.MOP1), _templates(dis.MOP2), _templates(dis.MOP3)
RAW = re.compile(r"(ALU1|ALU2|MOP1|MOP2|MOP3)\?(0x[0-9a-fA-F]+|\d+)$")
PREFIX = re.compile(r"^\s*[0-9A-Fa-f]{4}\s+[0-9A-Fa-f]{14}\s+")


def _addr(arg, labels=None):
    arg = re.sub(r"<[^>]*>", "", arg).strip()
    m = re.fullmatch(r"[DC]_([0-9A-Fa-f]+)", arg)
    if m:
        v = int(m.group(1), 16)
    elif re.fullmatch(r"0x[0-9a-fA-F]+|\d+", arg):
        v = int(arg, 0)
    elif labels is not None and arg in labels:
        v = labels[arg]
    else:
        raise AsmError(f"bad address {arg!r}")
    if not 0 <= v <= 0x3FF:
        raise AsmError(f"address {arg!r} out of range")
    return v


def _mop(text, templates, what):
    """-> (code, address)"""
    if text == "NOP":
        return 0, 0
    m = RAW.match(text)
    if m and m.group(1) == what:
        return int(m.group(2), 0), 0
    for rx, code in templates:
        mm = rx.match(text)
        if mm:
            return code, _addr(mm.group(1))
    raise AsmError(f"{what}: unknown {text!r}")


def _alu(text, inv, what):
    if text in inv:
        return inv[text]
    m = RAW.match(text)
    if m and m.group(1) == what:
        return int(m.group(2), 0)
    raise AsmError(f"{what}: unknown {text!r}")


def assemble_line(text, labels):
    parts = [p.strip().replace(" ", "") for p in text.split("|")]
    if len(parts) != 5:
        raise AsmError(f"need 5 fields: {text!r}")
    a1 = _alu(parts[0], ALU1_INV, "ALU1")
    a2 = _alu(parts[1], ALU2_INV, "ALU2")
    m1, d1 = _mop(parts[2], MOP1_T, "MOP1")
    m2, d2 = _mop(parts[3], MOP2_T, "MOP2")
    p3 = parts[4]
    if p3.startswith("PCADDR("):
        m3, d3 = 0, _addr(p3[7:-1], labels)
    elif p3.startswith("DLYPTR("):
        m3, d3 = 0, _addr(p3[7:-1])
        if d3 == 0:
            raise AsmError("DLYPTR(0) is not encodable apart from NOP (MOP3 = 0, AD3 = 0)")
    else:
        m3, d3 = _mop(p3, MOP3_T, "MOP3")
    return a1 << 49 | a2 << 42 | m1 << 37 | d1 << 27 | m2 << 24 | d2 << 14 | m3 << 10 | d3


def _lines(src):
    for n, raw in enumerate(src.splitlines(), 1):
        line = PREFIX.sub("", raw.split(";", 1)[0]).strip()
        if line:
            yield n, line


def assemble(src):
    """Assembler source -> list of instruction words."""
    labels, pc = {}, 0
    for n, line in _lines(src):
        if line.endswith(":") and "|" not in line:
            labels[line[:-1].strip()] = pc
        else:
            pc += 1
    words = []
    for n, line in _lines(src):
        if line.endswith(":") and "|" not in line:
            continue
        try:
            words.append(assemble_line(line, labels))
        except AsmError as e:
            raise AsmError(f"line {n}: {e}") from None
    return words


def replace_program(blocks, words):
    """Blocks of a boot image with the DSP program replaced by words (one block at 0)."""
    data = b"".join(w.to_bytes(7, "little") for w in words)
    out, done = [], False
    for b in blocks:
        if b.mem == 2:
            if not done:
                out.append(dsp_image.Block(2, 0, data, 7))
                done = True
            continue
        out.append(b)
    if not done:
        raise AsmError("image has no DSP program block")
    return out


def check(words, fs=96000):
    """Hardware rules -> list of problems (empty: ok). Executes the frame path (JMP followed)."""
    import tas3108_flow
    errors = []
    order = tas3108_flow.trace(words)
    budget = CYCLES[fs]
    if len(order) > budget:
        errors.append(f"{len(order)} cycles per sample, budget {budget} at {fs} Hz")
    last_dly, last_comp = None, None
    for i, pc in enumerate(order):
        a1, a2, m1, d1, m2, d2, m3, d3 = dis.fields(words[pc])
        if a1 in dis.BRANCH and a1 != 0x15:
            if pc >= JUMP_LIMIT:  # the target has 10 bits anyway
                errors.append(f"0x{pc:03X}: branch instruction beyond the first 1K")
            if a2 or m1 or d1 or m2 or d2 or m3:
                errors.append(f"0x{pc:03X}: branch with other operations")
            for k in range(1, BRANCH_NOPS + 1):
                if pc + k >= len(words) or words[pc + k] != 0:
                    errors.append(f"0x{pc:03X}: branch not followed by {BRANCH_NOPS} NOPs")
                    break
            if a1 in (0x13, 0x14) and last_comp is not None and i - last_comp < COMP_LATENCY:
                errors.append(f"0x{pc:03X}: branch {i - last_comp} cycles after COMP")
        # a conditional load uses the result of an earlier COMP; a COMP in the same instruction
        # only affects later ones
        if (m1 in (0x05, 0x06, 0x07, 0x08, 0x09, 0x14) or m2 in (0x2, 0x7)) and last_comp is not None \
                and i - last_comp < COMP_LATENCY:
            errors.append(f"0x{pc:03X}: conditional load {i - last_comp} cycles after COMP")
        if a2 in (0x08, 0x10, 0x18, 0x20):
            last_comp = i
        if a1 not in dis.BRANCH and m3 == 0 and d3:
            last_dly = i
        if m3 == 4:
            if last_dly is None or i - last_dly < DLY_DISTANCE:
                errors.append(f"0x{pc:03X}: ST(DLYO) {('-' if last_dly is None else i - last_dly)} "
                              f"instructions after DLYPTR, need {DLY_DISTANCE}")
        for what, a in (("data", d1 if (m1 or d1) else None), ("coef", d2 if (m2 or d2) else None),
                        ("store", d3 if (m3 and a1 not in dis.BRANCH) else None)):
            if a is not None and a >= MEM_WORDS and m3 != 4:
                errors.append(f"0x{pc:03X}: {what} address 0x{a:03X} beyond {MEM_WORDS} words")
    return errors


def main(argv):
    if len(argv) >= 3 and argv[0] == "asm":
        words = assemble(open(argv[1]).read())
        if "--image" in argv:
            orig = argv[argv.index("--image") + 1]
            blocks, rest = dsp_image.parse(open(orig, "rb").read())
            open(argv[2], "wb").write(dsp_image.build(replace_program(blocks, words), rest))
        else:
            open(argv[2], "wb").write(b"".join(w.to_bytes(7, "little") for w in words))
        print(f"{len(words)} instructions")
        return 0
    if len(argv) >= 2 and argv[0] == "check":
        fs = int(argv[argv.index("--fs") + 1]) if "--fs" in argv else 96000
        blocks, _ = dsp_image.parse(open(argv[1], "rb").read())
        errors = check(dis.program(blocks), fs)
        for e in errors:
            print("ERROR", e)
        return 1 if errors else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
