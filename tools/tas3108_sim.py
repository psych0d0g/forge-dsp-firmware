#!/usr/bin/env python3
"""Cycle level simulator of the TAS3108 DSP core for equivalence checks (issue #47).

Purpose: show that a rewritten program computes the same outputs as the original, sample by
sample and bit for bit, for random inputs and coefficients. The model only has to be
deterministic and sensitive to ordering errors; where the hardware is not documented exactly
(output scaling, LOG2/ALOG2 approximation, ALU2 0x4F) both programs see the same model.

Timing model (as tas3108_flow.py, consistent with SLEU067A and the Forge biquad kernels):
  - an instruction reads the register state before it, its writes are visible in the next one;
  - MR = MD * MC of the previous state (multiply every cycle);
  - stores write the old register value, loads are visible in the next instruction;
  - COMP sets the compare flag for instructions two or more cycles later (TAS3xxx guide);
  - DLYPTR(p) pushes the DLYI result of the same instruction into delay line p; DLYO takes the
    line output 14 instructions later (13 cycles between), ST(DLYO) before that stores the
    previous DLYO value;
  - ALU2 0x4F (undocumented) clears MC: the volume blocks only produce the stored gain if MD or
    MC is zero afterwards.
Number formats: data 25.23 in 48 bits, coefficients 5.23 in 28 bits, MR/ACC 30.46 in 76 bits;
48-bit operands enter the adder as value << 23 (SLEU067A section 3.4.6.2).

Usage:
    tas3108_sim.py compare A.bin B.bin [--frames N] [--seed S]
                          run two boot images with the same random inputs and coefficients and
                          compare DO1..DO8 of every sample (exit 1 on the first difference)
License: MIT
"""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsp_image  # noqa: E402
import tas3108_dis as dis  # noqa: E402

M48, M28, M76, M32 = (1 << 48) - 1, (1 << 28) - 1, (1 << 76) - 1, (1 << 32) - 1


def sx(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def clip(v, bits):
    hi = (1 << (bits - 1)) - 1
    return hi if v > hi else -hi - 1 if v < -hi - 1 else v


def _log2(v):
    """Deterministic stand-in for the LOG2 estimator (5.4 result aligned as 25.23)."""
    v = abs(v)
    if v == 0:
        return 0
    n = v.bit_length()
    frac = ((v << 4) >> (n - 1)) & 0xF
    return ((n << 4) | frac) << 19


def _alog2(v):
    e = (v >> 23) & 0x1F
    return clip((1 << e) << 18, 48)


class Core:
    def __init__(self, words, coef, data, delay_seed=None):
        self.code = [dis.fields(w) for w in words]
        self.coef = dict(coef)
        self.data = dict(data)
        self.delay = {}
        self.delay_seed = delay_seed
        self.dlen = {}
        self.r = dict(MD=0, MC=0, B=0, L=0, BR=0, LR=0, MR=0, ACC=0, DLYI=0)
        self.flags = []  # compare results by cycle

    def _acc_in(self, name, regs):
        v = regs[name]
        return v << 23 if name in ("BR", "LR") else v

    def frame(self, inputs):
        """One sample. inputs: 8 ints (24-bit). -> DO1..DO8 (None if not written)."""
        out = [None] * 8
        pc, cycle, ptr, steps = 0, 0, None, 0
        comp_hist = []  # (cycle, flag)
        while 0 <= pc < len(self.code) and steps < 10000:
            steps += 1
            a1, a2, m1, d1, m2, d2, m3, d3 = self.code[pc]
            old = self.r
            new = dict(old)
            new["MR"] = sx(old["MD"] * old["MC"], 76)
            flag = None
            for c, f in reversed(comp_hist):
                if c <= cycle - 2:
                    flag = f
                    break
            # ALU1
            if a1 == 0x0F:
                new["ACC"] = 0
            elif a1 == 0x01:
                new["BR"] = clip(abs(old["B"]), 48)
            elif a1 == 0x02:
                new["BR"] = clip(-old["B"], 48)
            elif a1 == 0x03:
                new["LR"] = clip(-old["L"], 48)
            elif a1 == 0x0E:
                new["LR"] = clip(abs(old["L"]), 48)
            elif a1 == 0x04:
                new["LR"] = _log2(old["L"])
            elif a1 == 0x05:
                new["LR"] = _alog2(old["L"])
            elif 0x06 <= a1 <= 0x09:
                new["BR"] = old["B"] >> (a1 - 5)
            elif 0x0A <= a1 <= 0x0D or a1 == 0x16:
                new["BR"] = sx(old["B"] << (20 if a1 == 0x16 else a1 - 9), 48)
            elif a1 == 0x10:
                new["BR"] = old["B"]
            elif a1 == 0x11:
                new["LR"] = old["L"]
            # ALU2
            if a2 == 0x4D:
                new["ACC"] = 0
            elif a2 == 0x4F:
                # undocumented; the volume block only gives the stored gain if MD or MC is zero
                # afterwards (otherwise ACC = BR + MR is about 64 times too large), so: MC = 0
                new["MC"] = 0
            elif a2 in (0x08, 0x10, 0x18, 0x20):
                a, b = {0x08: ("BR", "MR"), 0x10: ("BR", "LR"), 0x18: ("ACC", "MR"), 0x20: ("ACC", "LR")}[a2]
                av, bv = self._acc_in(a, old), self._acc_in(b, old)
                f = 1 if abs(av) > abs(bv) else 0 if abs(av) < abs(bv) else (1 if av >= 0 > bv else 0)
                comp_hist.append((cycle, f))
            elif a2 in _ADD:
                a, b, c, d = _ADD[a2]
                s = clip(self._acc_in(a, old) + (0 if b == "ZERO" else self._acc_in(b, old)), 76)
                if d == "ACC":
                    new["ACC"] = s
                elif d.startswith("DO"):
                    out[int(d[2:]) - 1] = clip(s >> 23, 32) & M32
                else:
                    v = s >> 23
                    v = clip(v, 28) if c == "CLP28" or d == "MC" else clip(v, 32) if c == "CLP32" else clip(v, 48)
                    if d == "MDB":
                        new["MD"] = new["B"] = v
                        new["ACC"] = 0
                    else:
                        new[d] = v
            # loads
            if m1 or d1:
                name = dis.MOP1.get(m1, "")
                if name:
                    cond = name[:3]
                    if cond == "LD(" or (cond == "LDC" and flag == 1) or (cond == "LNC" and flag == 0):
                        v = self.data.get(d1, 0)
                        dst = name[name.index("},") + 2:-1]
                        for reg in ("B", "L") if dst == "BL" else (dst,):
                            new[reg] = clip(v, 28) if reg == "MC" else v
            if m2 or d2:
                name = dis.MOP2.get(m2, "")
                if name:
                    cond = name[:3]
                    if cond == "LD(" or (cond == "LDC" and flag == 1) or (cond == "LNC" and flag == 0):
                        new[name[name.index("},") + 2:-1]] = self.coef.get(d2, 0)
            # stores, delay, branches
            if a1 in dis.BRANCH:
                if a1 == 0x15:
                    break
                take = a1 == 0x12 or (a1 == 0x13 and flag == 1) or (a1 == 0x14 and flag == 0)
                self.r = new
                cycle += 1 + 3  # the three NOPs after a branch run through the pipe
                pc = d3 if take else pc + 1
                continue
            if m3 == 0 and d3:
                ptr = d3
                if ptr not in self.delay:
                    n = 3 + ptr % 7
                    if self.delay_seed is None:
                        self.delay[ptr] = [0] * n
                    else:
                        r = random.Random(self.delay_seed * 1009 + ptr)
                        self.delay[ptr] = [r.randrange(-(1 << 22), 1 << 22) for _ in range(n)]
                line = self.delay[ptr]
                line.append(clip(new["DLYI"], 24))  # 24-bit delay memory
                # DLYO holds the new value only 14 instructions later (13 cycles between)
                self.dly_pending = (cycle + 14, line.pop(0))
            elif m3 == 6:
                self.data[d3] = sx(inputs[d3 & 7], 48)
            elif m3 in (1, 2, 3):
                self.data[d3] = old[{1: "MD", 2: "B", 3: "L"}[m3]]
            elif m3 == 4:
                ready, value = getattr(self, "dly_pending", (0, 0))
                if cycle >= ready:
                    self.dlyo = value
                self.data[d3] = getattr(self, "dlyo", 0)
            elif m3 in (7, 8, 9):
                self.coef[d3] = clip(old[{7: "MD", 8: "B", 9: "L"}[m3]], 28)
            self.r = new
            cycle += 1
            pc += 1
        return out


_ADD = {}
for _code, _name in dis.ALU2.items():
    if _name.startswith("ADD("):
        _ADD[_code] = tuple(_name[4:-1].split(","))


def initial_memory(blocks, rng):
    """Coefficients and data of the image; I2C coefficient registers get random, stable values
    (biquads with small poles, gains below 1) so that every path carries signal."""
    coef, data = {}, {}
    for b in blocks:
        if b.mem == 3:
            for i, w in enumerate(b.words):
                coef[b.addr + i] = sx(int.from_bytes(w, "little"), 28)
        elif b.mem == 4:
            for i, w in enumerate(b.words):
                data[b.addr + i] = sx(int.from_bytes(w, "little"), 48)
    for s, a, n, kind in dsp_image.regmap(blocks):
        if kind != 1:
            continue
        if n == 20:  # b0 near 1, the rest small: long chains keep their level
            vals = [rng.uniform(0.9, 1.1)] + [rng.uniform(-0.1, 0.1) for _ in range(4)]
        else:
            vals = [rng.uniform(0.5, 1.0) for _ in range(n // 4)]
        for k, v in enumerate(vals):
            coef[a + k] = int(v * (1 << 23))
    return coef, data


def compare(img_a, img_b, frames=64, seed=1, random_state=True):
    """-> None if equal, else a description of the first difference.
    random_state: every data word starts with the same small random value in both programs, so
    smoothers and filter states carry signal from the first sample. This requires the same data
    memory layout; for programs with a different layout use random_state=False and enough
    frames for the volume ramps (several hundred)."""
    rng = random.Random(seed)
    ba, _ = dsp_image.parse(img_a)
    bb, _ = dsp_image.parse(img_b)
    coef, data = initial_memory(ba, random.Random(seed))
    coef_b, data_b = initial_memory(bb, random.Random(seed))
    if random_state:
        r = random.Random(seed + 1)
        state = {a: r.randrange(-(1 << 22), 1 << 22) for a in range(1022)}
        data, data_b = {**state, **data}, {**state, **data_b}
    if coef != coef_b:
        return "coefficient memory differs (registers or constants moved)"
    ds = seed if random_state else None
    a = Core(dis.program(ba), coef, data, ds)
    b = Core(dis.program(bb), coef_b, data_b, ds)
    for n in range(frames):
        ins = [rng.randrange(-(1 << 23), 1 << 23) for _ in range(8)]
        oa, ob = a.frame(ins), b.frame(ins)
        if oa != ob:
            ch = [i + 1 for i in range(8) if oa[i] != ob[i]]
            return f"sample {n}: DO{ch} differ ({[oa[i - 1] for i in ch]} vs {[ob[i - 1] for i in ch]})"
        if random_state:  # same layout: everything the MCU could read must match too
            for what, ma, mb in (("data", a.data, b.data), ("coef", a.coef, b.coef)):
                diff = sorted(k for k in set(ma) | set(mb) if ma.get(k) != mb.get(k))
                if diff:
                    return f"sample {n}: {what} memory differs at {', '.join(f'0x{k:03X}' for k in diff[:8])}"
    return None


def main(argv):
    if len(argv) >= 3 and argv[0] == "compare":
        frames = int(argv[argv.index("--frames") + 1]) if "--frames" in argv else 64
        seed = int(argv[argv.index("--seed") + 1]) if "--seed" in argv else 1
        r = compare(open(argv[1], "rb").read(), open(argv[2], "rb").read(), frames, seed)
        print(r or f"equal over {frames} samples")
        return 1 if r else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
