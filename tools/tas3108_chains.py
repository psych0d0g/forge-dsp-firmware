#!/usr/bin/env python3
"""Biquad chains of the Forge DSP programs (issue #47, step 2: program generator).

The programs compute every biquad chain in segments. A segment of n biquads:

    head    5 instructions: CLRACC | LD(D,MD) | LD(C_c+2,MC); NOP | LD(D,MD) | LD(C_c+1,MC);
            then three times ADD | LD(D,MD) | LD(C,MC) | ST(MD,…), the first loads the input D_in and C_c
    period  5 instructions per further biquad (pipelined: they finish the previous biquad and start
            the next): ADD | LD(C); ADD MDB | LD(C); ADD | LD(D) | LD(C) | ST(B); 2 x ADD | LD(D) | LD(C) | ST(MD)
    tail    ADD; ADD(…,B); CLRACC … ST(B,DATA,D_out)                                 3 instructions
    A segment of n biquads has 5 n + 3 instructions and the coefficients C_c … C_c+5n-1.

A chain is a sequence of segments where each segment takes the output of the previous one as
its input (D_in = D_out of the previous). This module finds the segments by their pattern and
links them to chains.

    tas3108_chains.py IMAGE.bin      list the chains with their segments
License: MIT
"""
import re
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsp_image  # noqa: E402
import tas3108_dis as dis  # noqa: E402

H0 = re.compile(r"^NOP \| CLRACC \| LD\(D_([0-9A-F]+),MD\) \| LD\(C_([0-9A-F]+),MC\) \| NOP$")
H1 = re.compile(r"^NOP \| NOP \| LD\(D_([0-9A-F]+),MD\) \| LD\(C_([0-9A-F]+),MC\) \| NOP$")
# loads a data word and a coefficient and stores MD: the input load of the head and periods 4/5
M = re.compile(r"^NOP \| ADD\(ACC,MR,NONE,ACC\) \| LD\(D_([0-9A-F]+),MD\) \| LD\(C_([0-9A-F]+),MC\) \| ST\(MD,DATA,D_([0-9A-F]+)\)$")
P1 = re.compile(r"^NOP \| ADD\(ACC,MR,NONE,ACC\) \| NOP \| LD\(C_([0-9A-F]+),MC\) \| NOP$")
P2 = re.compile(r"^NOP \| ADD\(ACC,MR,NONE,MDB\) \| NOP \| LD\(C_([0-9A-F]+),MC\) \| NOP$")
P3 = re.compile(r"^NOP \| ADD\(ACC,MR,NONE,ACC\) \| LD\(D_([0-9A-F]+),MD\) \| LD\(C_([0-9A-F]+),MC\) \| ST\(B,DATA,D_([0-9A-F]+)\)$")
T0 = "NOP | ADD(ACC,MR,NONE,ACC) | NOP | NOP | NOP"
T1 = "NOP | ADD(ACC,MR,NONE,B) | NOP | NOP | NOP"
T2 = re.compile(r"^NOP \| CLRACC \| NOP \| NOP \| ST\(B,DATA,D_([0-9A-F]+)\)$")
PERIOD = [P1, P2, P3, M, M]


class Segment:
    """x1, x2: input history; ys: (y1, y2) of every biquad; coefs: first coefficient (b0) of every
    biquad, followed by b1, b2, a1, a2. The output of the segment is y1 of its last biquad."""

    def __init__(self, start, src, x1, x2, ys, coefs, extra=None):
        self.start, self.src, self.x1, self.x2, self.ys, self.coefs = start, src, x1, x2, ys, coefs
        self.extra = dict(extra or {})  # offset in the segment -> store field merged into that line

    n = property(lambda self: len(self.ys))
    dst = property(lambda self: self.ys[-1][0])
    coef0 = property(lambda self: self.coefs[0])
    data = property(lambda self: {self.x1, self.x2} | {a for y in self.ys for a in y})

    @property
    def end(self):  # first instruction after the segment: head 5, (n - 1) periods of 5, tail 3
        return self.start + 5 * self.n + 3

    def __repr__(self):
        return (f"Segment(@{self.start:03X}, {self.n} biquads, D_{self.src:03X} -> D_{self.dst:03X}, "
                f"C_{self.coef0:03X}.., D_{min(self.data):03X}..D_{max(self.data):03X})")


def _text(w):
    return dis.disasm(w)[0]


FREE3 = (H0, H1, P1, P2)   # lines whose store field is free (the optimizer may merge a store there)
MOP3 = 0x3FFF                # store field of an instruction word (MOP3 << 10 | address)


def _match(pat, w, extra, off):
    """match one line; a store the template does not have is accepted in a free store field and
    recorded in extra[off]"""
    t = _text(w)
    m = pat.match(t) if hasattr(pat, "match") else (t == pat or None)
    if m or not (pat in FREE3 or pat in (T0, T1)) or not (w & MOP3):
        return m
    t = _text(w & ~MOP3)
    m = pat.match(t) if hasattr(pat, "match") else (t == pat or None)
    if m:
        extra[off] = w & MOP3
    return m


def segments(words):
    """all biquad segments of a program, in program order"""
    out, i = [], 0
    h = lambda r, k: int(r.group(k), 16)
    while i + 8 < len(words):
        extra = {}
        h0, h1 = _match(H0, words[i], extra, 0), _match(H1, words[i + 1], extra, 1)
        m = [_match(M, words[i + k], extra, k) for k in (2, 3, 4)]
        if not (h0 and h1 and all(m)):
            i += 1
            continue
        x2, x1, src, c = h(h0, 1), h(h1, 1), h(m[0], 1), h(m[0], 2)
        ys, coefs = [(h(m[1], 1), h(m[2], 1))], [c]
        j = i + 5
        while j + 5 <= len(words):
            e = {}
            p = [_match(PERIOD[k], words[j + k], e, j - i + k) for k in range(5)]
            if not all(p):
                break
            extra.update(e)
            coefs.append(h(p[1], 1))                    # b0 of this biquad is loaded in the second line
            ys.append((h(p[3], 1), h(p[4], 1)))
            j += 5
        ok = j + 2 < len(words) and _match(T0, words[j], extra, j - i) and _match(T1, words[j + 1], extra, j - i + 1)
        tail = T2.match(_text(words[j + 2])) if ok else None
        if tail and h(tail, 1) == ys[-1][0]:
            seg = Segment(i, src, x1, x2, ys, coefs, extra)
            if emit_segment(seg) == words[i:i + 5 * seg.n + 3]:   # only what the template reproduces exactly
                out.append(seg)
                i = j + 3
                continue
        i += 1
    return out


def emit_segment(seg):
    """instruction words of a segment (the template above)"""
    import tas3108_asm as asm
    D = lambda a: f"D_{a:03X}"
    C = lambda a: f"C_{a:03X}"
    c, (y1, y2) = seg.coefs[0], seg.ys[0]
    lines = [
        f"NOP | CLRACC | LD({D(seg.x2)},MD) | LD({C(c + 2)},MC) | NOP",
        f"NOP | NOP | LD({D(seg.x1)},MD) | LD({C(c + 1)},MC) | NOP",
        f"NOP | ADD(ACC,MR,NONE,ACC) | LD({D(seg.src)},MD) | LD({C(c)},MC) | ST(MD,DATA,{D(seg.x2)})",
        f"NOP | ADD(ACC,MR,NONE,ACC) | LD({D(y1)},MD) | LD({C(c + 3)},MC) | ST(MD,DATA,{D(seg.x1)})",
        f"NOP | ADD(ACC,MR,NONE,ACC) | LD({D(y2)},MD) | LD({C(c + 4)},MC) | ST(MD,DATA,{D(y2)})",
    ]
    for (py1, py2), c, (y1, y2) in zip(seg.ys, seg.coefs[1:], seg.ys[1:]):
        lines += [
            f"NOP | ADD(ACC,MR,NONE,ACC) | NOP | LD({C(c + 2)},MC) | NOP",
            f"NOP | ADD(ACC,MR,NONE,MDB) | NOP | LD({C(c)},MC) | NOP",
            f"NOP | ADD(ACC,MR,NONE,ACC) | LD({D(py1)},MD) | LD({C(c + 1)},MC) | ST(B,DATA,{D(py1)})",
            f"NOP | ADD(ACC,MR,NONE,ACC) | LD({D(y1)},MD) | LD({C(c + 3)},MC) | ST(MD,DATA,{D(py2)})",
            f"NOP | ADD(ACC,MR,NONE,ACC) | LD({D(y2)},MD) | LD({C(c + 4)},MC) | ST(MD,DATA,{D(y2)})",
        ]
    lines += ["NOP | ADD(ACC,MR,NONE,ACC) | NOP | NOP | NOP", "NOP | ADD(ACC,MR,NONE,B) | NOP | NOP | NOP",
              f"NOP | CLRACC | NOP | NOP | ST(B,DATA,{D(seg.dst)})"]
    out = [asm.assemble_line(t, {}) for t in lines]
    for off, bits in seg.extra.items():
        if off >= len(out) or out[off] & MOP3:
            raise ValueError(f"merged store at {off} does not fit a segment of {seg.n} biquads")
        out[off] |= bits
    return out


def chains(words):
    """segments linked to chains: a segment whose input is the output of the previous one continues it"""
    result = []
    for s in segments(words):
        for c in result:
            if c[-1].dst == s.src:
                c.append(s)
                break
        else:
            result.append([s])
    return result


class Slot:
    """one biquad: its five coefficients from coef (b0) on, and its output history (y1, y2)"""

    def __init__(self, coef, y):
        self.coef, self.y = coef, y

    def __repr__(self):
        return f"Slot(C_{self.coef:03X}, D_{self.y[0]:03X}/D_{self.y[1]:03X})"


def _retarget_loads(word, old, new):
    """the instruction with its data loads from D_old changed to D_new (None if it has none)"""
    import tas3108_asm as asm
    text, unknown = dis.disasm(word)
    tag = f"LD(D_{old:03X},"
    if unknown or tag not in text:
        return None
    return asm.assemble_line(text.replace(tag, f"LD(D_{new:03X},"), {})


def relayout(words, lengths):
    """The program with the biquad chains at the given lengths (one per chain, in the order of
    chains()). Biquads move between chains of the same program: the ones a chain gives up (from its
    end) are appended to the chains that grow, in chain order. Segment boundaries stay, the last
    segment of a chain takes the change; a segment that loses all its biquads disappears. The code
    after a chain that reads its output is pointed to the new output word.
    -> (words, moved): moved = slots that changed their chain."""
    cs = chains(words)
    if len(lengths) != len(cs):
        raise ValueError(f"{len(cs)} chains, {len(lengths)} lengths given")
    have = [[Slot(c, y) for s in ch for c, y in zip(s.coefs, s.ys)] for ch in cs]
    if sum(lengths) != sum(map(len, have)):
        raise ValueError(f"the pool has {sum(map(len, have))} biquads, the lengths need {sum(lengths)}")
    if min(lengths) < 1:
        raise ValueError("every chain keeps at least one biquad")
    free = []
    for slots, n in zip(have, lengths):
        while len(slots) > n:
            free.insert(0, slots.pop())
    free.reverse()
    moved = list(free)
    for slots, n in zip(have, lengths):
        while len(slots) < n:
            slots.append(free.pop(0))
    # new segments per chain: original boundaries, the last kept segment takes the rest
    new_segs = {}
    retarget = {}
    for ch, slots in zip(cs, have):
        k, out = 0, []
        for idx, seg in enumerate(ch):
            last = idx == len(ch) - 1 or k + seg.n >= len(slots)
            take = slots[k:] if last else slots[k:k + seg.n]
            if not take:
                break
            src = out[-1].dst if out else seg.src
            out.append(Segment(seg.start, src, seg.x1, seg.x2, [t.y for t in take], [t.coef for t in take]))
            k += len(take)
            if last:
                break
        for seg in ch:
            new_segs[seg.start] = None
        for seg in out:
            new_segs[seg.start] = seg
        if out[-1].dst != ch[-1].dst:
            retarget[ch[-1].dst] = out[-1].dst
    old_by_start = {seg.start: seg for ch in cs for seg in ch}
    result, i = [], 0
    while i < len(words):
        if i in old_by_start:
            seg = new_segs[i]
            if seg is not None:
                result += emit_segment(seg)
            i += 5 * old_by_start[i].n + 3
            continue
        w = words[i]
        for old, new in retarget.items():
            r = _retarget_loads(w, old, new)
            if r is not None:
                w = r
        result.append(w)
        i += 1
    return result, moved


def registers(words, blocks):
    """I2C subaddress of every biquad, per chain in chain order (the 8051 register table maps a
    subaddress to the five coefficients of one biquad; a biquad keeps its subaddress when it moves)"""
    by_coef = {a: sub for sub, a, n, kind in dsp_image.regmap(blocks) if kind == 1 and n == 20}
    return [[by_coef[c] for s in ch for c in s.coefs] for ch in chains(words)]


def main(argv):
    if len(argv) != 1:
        print(__doc__)
        return 2
    blocks, _ = dsp_image.parse(open(argv[0], "rb").read())
    words = dis.program(blocks)
    regs = registers(words, blocks)
    for k, c in enumerate(chains(words), 1):
        print(f"chain {k}: {sum(s.n for s in c)} biquads, D_{c[0].src:03X} -> D_{c[-1].dst:03X}, "
              f"registers 0x{regs[k - 1][0]:02X}…0x{regs[k - 1][-1]:02X}")
        for s in c:
            print("   ", s)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
