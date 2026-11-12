#!/usr/bin/env python3
"""Dynamic biquad pools (issue #47, steps 3/4): tables for the MCU and the reference generator.

Every DSP core has one pool of biquads. The channels of a core own consecutive parts of the pool
in the order of their virtual biquad numbers (data_flow_ch_bq_offsets): with the lengths
L1, L2, … channel 1 owns pool slots 0 … L1-1, channel 2 the next L2 and so on. A pool slot keeps
its coefficient address, its state words and its I2C subaddress, so data_flow_dsp_bq_core/reg
and the storage of a virtual biquad number stay as they are; only the offsets and limits of the
channels change.

The MCU builds the DSP program from the optimized image and these tables while it writes the
DSP's EEPROM (src/dsp_pool.c). Table of one program (little endian words):

    header  prog_off, prog_len (image offset and bytes of the program block), n_pieces,
            n_chains, n_segs, n_slots, n_patches, n_channels, then the byte offsets of
            pieces, chains, segs, slots, patches, channels, templates
    pieces  (a, b): literal words a … a+b-1 of the base program; a = 0x8000 | chain << 8 | seg:
            segment seg of chain
    chains  (pool channel q, number of segments, first segment index (word))
    segs    (biquads of the original, offset of a merged store or 0xFF, the store bits (word),
             x1, x2, input (0xFFFF = output of the previous segment))
    slots   (b0 coefficient address, y1, y2) in pool order
    patches (word index, chain, fields): a literal word that loads (fields bit 0, MOP1) or stores
            back (bit 1, MOP3) the output of that chain
    channels (channel number, default length, minimum length) in pool order
    templates 9 instruction words of 7 bytes with zero addresses (H0 H1 M P1 P2 P3 T0 T1 T2)

    dsp_pool.py FLAVOUR_DATA.h N IMG.bin  show the pool of program N of an (optimized) image
License: MIT
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dsp_image  # noqa: E402
import tas3108_asm as asm  # noqa: E402
import tas3108_chains as tc  # noqa: E402
import tas3108_dis as dis  # noqa: E402

TEMPLATES = ["H0", "H1", "M", "P1", "P2", "P3", "T0", "T1", "T2"]
_TPL_TEXT = {
    "H0": "NOP | CLRACC | LD(D_000,MD) | LD(C_000,MC) | NOP",
    "H1": "NOP | NOP | LD(D_000,MD) | LD(C_000,MC) | NOP",
    "M": "NOP | ADD(ACC,MR,NONE,ACC) | LD(D_000,MD) | LD(C_000,MC) | ST(MD,DATA,D_000)",
    "P1": "NOP | ADD(ACC,MR,NONE,ACC) | NOP | LD(C_000,MC) | NOP",
    "P2": "NOP | ADD(ACC,MR,NONE,MDB) | NOP | LD(C_000,MC) | NOP",
    "P3": "NOP | ADD(ACC,MR,NONE,ACC) | LD(D_000,MD) | LD(C_000,MC) | ST(B,DATA,D_000)",
    "T0": "NOP | ADD(ACC,MR,NONE,ACC) | NOP | NOP | NOP",
    "T1": "NOP | ADD(ACC,MR,NONE,B) | NOP | NOP | NOP",
    "T2": "NOP | CLRACC | NOP | NOP | ST(B,DATA,D_000)",
}
TPL = {k: asm.assemble_line(v, {}) for k, v in _TPL_TEXT.items()}
D1, D2 = 27, 14   # bit positions of the MOP1 / MOP2 address; MOP3 address at bit 0
AMASK = 0x3FF


def table(path, name):
    """a BAS_DATA_TABLE of flavour_data.h as list of items (types b or w)"""
    t = open(path).read()
    m = re.search(r"/\* %s: (\d+) items, types (\w+) \*/\s*BAS_DATA_TABLE\(%s, \d+\) = \{(.*?)\};"
                  % (name, name), t, re.S)
    if not m:
        raise SystemExit(f"{name} not in {path}")
    b = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(3)))
    if m.group(2) == "b":
        return list(b)
    if m.group(2) == "w":
        return [b[i] | b[i + 1] << 8 for i in range(0, len(b), 2)]
    raise SystemExit(f"{name}: types {m.group(2)}")


def core_channels(path):
    """{core: [(channel, offset, limit), …] in pool order}"""
    lim, off = table(path, "data_flow_ch_bq_limits"), table(path, "data_flow_ch_bq_offsets")
    core = table(path, "data_flow_dsp_bq_core")
    out = {}
    for ch in range(1, len(lim)):
        if lim[ch]:
            out.setdefault(core[off[ch] + 1], []).append((ch, off[ch], lim[ch]))
    for k in out:
        out[k].sort(key=lambda c: c[1])
        pos = out[k][0][1]
        for ch, o, n in out[k]:
            if o != pos:
                raise SystemExit(f"core {k}: channel {ch} does not follow the previous one in the pool")
            pos += n
    return out


class Program:
    """pool tables of one DSP program (built from the base image)"""

    def __init__(self, img, channels, bq_reg):
        """channels: [(channel, offset, limit)] of one core in pool order; bq_reg: data_flow_dsp_bq_reg"""
        self.img = img
        self.blocks, self.rest = dsp_image.parse(img)
        progs = [b for b in self.blocks if b.mem == 2]
        if len(progs) != 1 or progs[0].addr != 0:
            raise SystemExit("need one DSP program block at address 0")
        self.prog_off, self.prog_len = progs[0].offset, 12 + len(progs[0].data) + progs[0].trailer_len
        self.words = dis.program(self.blocks)
        chains = tc.chains(self.words)
        regs = tc.registers(self.words, self.blocks)
        # chain <-> pool channel by the subaddress sequence
        self.channels, self.chains, self.slots = [], [], []
        order = {}
        for q, (ch, off, n) in enumerate(channels):
            want = [bq_reg[off + 1 + j] for j in range(n)]
            hit = [c for c in range(len(chains)) if regs[c] == want]
            if len(hit) != 1:
                raise SystemExit(f"channel {ch}: no chain with its biquad registers")
            order[hit[0]] = q
            self.channels.append([ch, n, 1])
            for s in chains[hit[0]]:
                self.slots += [(c, y1, y2) for c, (y1, y2) in zip(s.coefs, s.ys)]
        if len(order) != len(chains):
            raise SystemExit("chains without a channel")
        # slots in pool order: chains in the order of their channels
        self.slots = []
        for q in range(len(channels)):
            c = next(c for c, qq in order.items() if qq == q)
            self.slots += [(co, y1, y2) for s in chains[c] for co, (y1, y2) in zip(s.coefs, s.ys)]
        self.segs, starts = [], {}
        for c, ch in enumerate(chains):
            self.chains.append((order[c], len(ch), len(self.segs)))
            for k, s in enumerate(ch):
                if len(s.extra) > 1:
                    raise SystemExit("more than one merged store in a segment")
                eo, eb = next(iter(s.extra.items())) if s.extra else (0xFF, 0)
                self.segs.append((s.n, eo, eb, s.x1, s.x2, s.src if k == 0 else 0xFFFF))
                starts[s.start] = (c, k, 5 * s.n + 3)
        self.pieces, i, lit = [], 0, None
        while i < len(self.words):
            if i in starts:
                c, k, n = starts[i]
                self.pieces.append((0x8000 | c << 8 | k, 0))
                i += n
                lit = None
                continue
            if lit is None:
                self.pieces.append([i, 0])
                lit = self.pieces[-1]
            lit[1] += 1
            i += 1
        self.pieces = [tuple(p) for p in self.pieces]
        # literal words that read a chain output; nothing else may touch a slot's state words
        dst = {ch[-1].dst: c for c, ch in enumerate(chains)}
        state = {a for _, y1, y2 in self.slots for a in (y1, y2)}
        self.patches = []
        for a, b in self.pieces:
            if a & 0x8000:
                continue
            for i in range(a, a + b):
                f = dis.fields(self.words[i])
                fld = txt = dis.disasm(self.words[i])[0]
                part = txt.split(" | ")
                mask, c = 0, None
                if f[3] in dst and part[2].startswith(f"LD(D_{f[3]:03X},"):
                    mask, c = 1, dst[f[3]]
                    part[2] = "-"
                if f[7] in dst and part[4].startswith("ST(") and part[4].endswith(f",D_{f[7]:03X})"):
                    if c is not None and c != dst[f[7]]:
                        raise SystemExit(f"word 0x{i:03X} reads and writes outputs of different chains")
                    mask, c = mask | 2, dst[f[7]]
                    part[4] = "-"
                if mask:
                    self.patches.append((i, c, mask))
                used = {int(x, 16) for x in re.findall(r"D_([0-9A-F]{3})", " | ".join(part))}
                if used & state:
                    raise SystemExit(f"word 0x{i:03X} uses biquad state outside the chains: {fld}")
        # minimum length per channel: the first segment must hold its merged store
        for c, (q, nseg, s0) in enumerate(self.chains):
            n, eo = self.segs[s0][0], self.segs[s0][1]
            mn = 1
            while eo != 0xFF and 5 * mn + 3 <= eo:
                mn += 1
            self.channels[q][2] = mn
        if self.generate(self.default_lengths()) != self.words:
            raise SystemExit("the tables do not reproduce the base program")

    def default_lengths(self):
        return [n for _, n, _ in self.channels]

    def check_lengths(self, lengths):
        if len(lengths) != len(self.channels):
            raise ValueError("one length per channel")
        if sum(lengths) != len(self.slots):
            raise ValueError(f"the pool has {len(self.slots)} biquads, the lengths need {sum(lengths)}")
        for (ch, _, mn), n in zip(self.channels, lengths):
            if n < mn:
                raise ValueError(f"channel {ch}: at least {mn} biquads")

    # --- reference generator: the MCU does exactly this (src/dsp_pool.c) ---
    def generate(self, lengths):
        self.check_lengths(lengths)
        off, o = [], 0
        for n in lengths:
            off.append(o)
            o += n
        cdst = [self.slots[off[q] + lengths[q] - 1][1] for q, _, _ in self.chains]
        patch = {i: (c, m) for i, c, m in self.patches}
        out = []
        for a, b in self.pieces:
            if not a & 0x8000:
                for i in range(a, a + b):
                    w = self.words[i]
                    if i in patch:
                        c, m = patch[i]
                        if m & 1:
                            w = w & ~(AMASK << D1) | cdst[c] << D1
                        if m & 2:
                            w = w & ~AMASK | cdst[c]
                    out.append(w)
                continue
            c, k = a >> 8 & 0x7F, a & 0xFF
            q, nseg, s0 = self.chains[c]
            first = sum(self.segs[s0 + j][0] for j in range(k))
            rest = lengths[q] - first
            if rest <= 0:
                continue                                  # segment vanished
            n_orig, eo, eb, x1, x2, src = self.segs[s0 + k]
            n = rest if k == nseg - 1 or rest <= n_orig else n_orig
            if src == 0xFFFF:
                src = self.slots[off[q] + first - 1][1]   # output of the previous segment
            out += self.segment(self.slots[off[q] + first:off[q] + first + n], x1, x2, src, eo, eb)
        return out

    @staticmethod
    def segment(slots, x1, x2, src, eo, eb):
        def w(t, d1=0, d2=0, d3=0):
            return TPL[t] | d1 << D1 | d2 << D2 | d3
        c, y1, y2 = slots[0]
        out = [w("H0", x2, c + 2), w("H1", x1, c + 1), w("M", src, c, x2), w("M", y1, c + 3, x1),
               w("M", y2, c + 4, y2)]
        for (_, py1, py2), (c, y1, y2) in zip(slots, slots[1:]):
            out += [w("P1", 0, c + 2), w("P2", 0, c), w("P3", py1, c + 1, py1), w("M", y1, c + 3, py2),
                    w("M", y2, c + 4, y2)]
        out += [w("T0"), w("T1"), w("T2", 0, 0, slots[-1][1])]
        if eo != 0xFF:
            out[eo] |= eb
        return out

    def image(self, lengths):
        return dsp_image.build(asm.replace_program(self.blocks, self.generate(lengths)), self.rest)

    # --- table for the MCU ---
    def c_table(self):
        parts = []
        parts.append(b"".join(struct.pack("<HH", a, b) for a, b in self.pieces))
        parts.append(b"".join(struct.pack("<BBH", q, n, s0) for q, n, s0 in self.chains))
        parts.append(b"".join(struct.pack("<BBHHHH", *s) for s in self.segs))
        parts.append(b"".join(struct.pack("<HHH", *s) for s in self.slots))
        parts.append(b"".join(struct.pack("<HBB", i, c, m) for i, c, m in self.patches))
        parts.append(b"".join(struct.pack("<BBB", *c) for c in self.channels))
        parts.append(b"".join(TPL[t].to_bytes(7, "little") for t in TEMPLATES))
        head_len = 2 * (8 + len(parts))
        offs, o = [], head_len
        for p in parts:
            offs.append(o)
            o += len(p)
        head = struct.pack("<8H", self.prog_off, self.prog_len, len(self.pieces), len(self.chains), len(self.segs),
                           len(self.slots), len(self.patches), len(self.channels))
        head += struct.pack(f"<{len(offs)}H", *offs)
        return head + b"".join(parts)


def programs(flavour_data, images):
    """{program number: Program} for the base images {1: img, 2: img}; also checks that cores 2 and 3
    (which run the same program) have the same biquad registers per channel"""
    cc = core_channels(flavour_data)
    reg = table(flavour_data, "data_flow_dsp_bq_reg")
    out = {1: Program(images[1], cc[1], reg), 2: Program(images[2], cc[2], reg)}
    if 3 in cc:
        p3 = Program(images[2], cc[3], reg)
        same = lambda p: (p.pieces, p.chains, p.segs, p.slots, p.patches, [c[1:] for c in p.channels])
        if same(p3) != same(out[2]):
            raise SystemExit("cores 2 and 3 differ in their pools")
    return out, cc


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    cc = core_channels(argv[0])
    reg = table(argv[0], "data_flow_dsp_bq_reg")
    n = int(argv[1])
    p = Program(open(argv[2], "rb").read(), cc[1 if n == 1 else 2], reg)
    print(f"program block @0x{p.prog_off:04X}, {p.prog_len} bytes; {len(p.pieces)} pieces, {len(p.segs)} segments, "
          f"{len(p.slots)} slots, {len(p.patches)} patches, table {len(p.c_table())} bytes")
    for ch, n, mn in p.channels:
        print(f"  channel {ch}: {n} biquads (minimum {mn})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
