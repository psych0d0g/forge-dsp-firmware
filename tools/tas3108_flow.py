#!/usr/bin/env python3
"""Signal flow of a TAS3108 DSP program (issue #43).

The program is executed symbolically for one frame (one sample): every register and memory word
holds the set of sources its value depends on. The result is a block graph: for every data word
the program writes and later reads in the same frame (a "wire"), and for the outputs DO1..DO8,
the wires, input channels, delay lines and I2C subaddresses it is computed from directly.

Sources:
  inN      input channel N (SDIN1L..SDIN4R = in1..in8, ST(DI,DATA,n))
  D_xxx    a wire, i.e. a data word written earlier in the same frame
  dlyN     delay line N (DLYPTR(N) ... ST(DLYO,DATA,x))
  sXX      a coefficient, named by its I2C subaddress (dsp_image.regmap); coefficients the DSP
           writes itself (ST(x,COEF,a)) carry the sources of the stored value
A data word read before it is written in the frame holds the previous sample (biquad memories,
smoothing, the outputs of the last frame); such reads are shown as ~D_xxx and are left out of
the graph except for the outputs, so feedback does not hide the structure.

Timing model (consistent with the examples of SLEU067A and the biquad kernels of the Forge
programs): an instruction reads the register state before it, its writes become visible in the
next instruction, MR = MD * MC of the previous state, stores write the old register value.

Approximations: conditional loads (LDC/LNC) keep the old sources too, conditional branches are
not taken, ALU2 0x4F (undocumented) is ignored.

Usage:
    tas3108_flow.py IMAGE.bin          block graph, one line per wire and output
    tas3108_flow.py IMAGE.bin --dot    the same as Graphviz dot
    tas3108_flow.py IMAGE.bin --terms  with the product terms signal x coefficient of every word
License: MIT
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsp_image  # noqa: E402
import tas3108_dis as dis  # noqa: E402

E = frozenset()
REGS = ("MD", "MC", "B", "L", "BR", "LR", "MR", "ACC", "DLYI")


def _add_ops():
    """ALU2 code -> (operand a, operand b, destination) for the ADD family."""
    ops = {}
    for code, name in dis.ALU2.items():
        if name.startswith("ADD("):
            a, b, _clip, d = name[4:-1].split(",")
            ops[code] = (a, b, d)
    return ops


ADD = _add_ops()


def _dst(name):
    return name[name.index("},") + 2:-1]


def trace(prog):
    """Instructions in execution order of one frame (JMP followed, STOP ends)."""
    pc, seen, out = 0, set(), []
    while 0 <= pc < len(prog) and pc not in seen:
        seen.add(pc)
        a1 = dis.fields(prog[pc])[0]
        out.append(pc)
        if a1 == 0x15:
            break
        pc = (prog[pc] & 0x3FF) if a1 == 0x12 else pc + 1
    return out


class Flow:
    def __init__(self, blocks):
        self.prog = dis.program(blocks)
        self.sub = {}
        for s, a, n, kind in dsp_image.regmap(blocks):
            if kind == 1:
                for k in range(n // 4):
                    self.sub[a + k] = f"s{s:02X}"
        self.order = trace(self.prog)
        self.r = {k: E for k in REGS}
        self.data, self.coef, self.delay = {}, {}, {}
        self.wires = {}   # D address -> sources, last write of the frame
        self.read = set()  # wires read later in the same frame
        self.writes = []  # D addresses in order of their first write
        self.out = {n: E for n in range(1, 9)}
        self._run()

    def _run(self):
        ptr = None
        for pc in self.order:
            a1, a2, m1, d1, m2, d2, m3, d3 = dis.fields(self.prog[pc])
            old, new = self.r, dict(self.r)
            new["MR"] = old["MD"] | old["MC"]
            # product terms: signal operand x coefficient address (c tags only come from MC loads)
            caddrs = [x for x in old["MC"] if x.startswith("c")]
            if caddrs:
                sigs = [x for x in old["MD"] if x[0] in "D~id"]
                new["MR"] = new["MR"] | {f"t{m}*{c[1:]}" for m in sigs for c in caddrs}
            if a1 == 0x0F:
                new["ACC"] = E
            elif a1 in (0x01, 0x02, 0x10, 0x16) or 0x06 <= a1 <= 0x0D:
                new["BR"] = old["B"]
            elif a1 in (0x03, 0x04, 0x05, 0x0E, 0x11):
                new["LR"] = old["L"]
            if a2 == 0x4D:
                new["ACC"] = E
            elif a2 in ADD:
                a, b, d = ADD[a2]
                v = old[a] | (E if b == "ZERO" else old[b])
                if d.startswith("DO"):
                    self.out[int(d[2:])] |= v
                elif d == "MDB":
                    new["MD"] = new["B"] = v
                    new["ACC"] = E
                else:
                    new[d] = v
            if (m1 or d1) and m1 in dis.MOP1:
                name = dis.MOP1[m1]
                v = self.data.get(d1, frozenset({f"~D_{d1:03X}"}))
                if d1 in self.wires:
                    self.read.add(d1)
                cond = name.startswith(("LDC", "LNC"))
                for reg in ("B", "L") if _dst(name) == "BL" else (_dst(name),):
                    new[reg] = (new[reg] | v) if cond else v
            if (m2 or d2) and m2 in dis.MOP2:
                name = dis.MOP2[m2]
                v = self.coef.get(d2, E) | ({self.sub[d2]} if d2 in self.sub else E) | {f"c{d2:03X}"}
                reg = _dst(name)
                new[reg] = (new[reg] | v) if name.startswith(("LDC", "LNC")) else v
            if a1 in dis.BRANCH:
                pass
            elif m3 == 0 and d3:
                ptr = d3
                self.delay[ptr] = new["DLYI"]
            elif m3 in (1, 2, 3, 4, 6):
                if m3 == 6:
                    v = frozenset({f"in{(d3 & 7) + 1}"})
                elif m3 == 4:
                    v = frozenset({f"dly{ptr}"})
                else:
                    v = old[{1: "MD", 2: "B", 3: "L"}[m3]]
                me = f"D_{d3:03X}"
                if me in v:  # rewritten from itself: resolve to the earlier sources
                    v = (v - {me}) | self.wires.get(d3, E)
                v = v - {f"~{me}"}
                if d3 not in self.wires:
                    self.writes.append(d3)
                self.wires[d3] = v
                # readers see the wire itself, not its sources
                self.data[d3] = frozenset({me}) if m3 not in (4, 6) else v
            elif m3 in (7, 8, 9):
                self.coef[d3] = old[{7: "MD", 8: "B", 9: "L"}[m3]]
            self.r = new

    def subaddresses(self):
        """All subaddresses that reach a stored word, a delay line or an output."""
        vals = list(self.wires.values()) + list(self.delay.values()) + list(self.out.values())
        return {x for v in vals for x in v if x.startswith("s")}

    def lines(self):
        def fmt(v, prev=False):
            sig = sorted(x for x in v if x[0] not in "sct" and (prev or not x.startswith("~")))
            subs = sorted(x[1:] for x in v if x.startswith("s"))
            return ", ".join(sig) or "-", " ".join(_ranges(subs))
        res = []
        feeding = {x[3:] for v in self.out.values() for x in v if x.startswith("~D_")}
        for a in self.writes:
            v = self.wires[a]
            if a not in self.read and f"{a:03X}" not in feeding:
                continue
            if v and all(x.startswith(("in", "dly")) for x in v) and len(v) == 1:
                res.append((f"D_{a:03X}", next(iter(v)), ""))
                continue
            sig, subs = fmt(v)
            res.append((f"D_{a:03X}", sig, subs))
        for ptr, v in sorted(self.delay.items()):
            sig, subs = fmt(v)
            res.append((f"dly{ptr}", sig, subs))
        for n, v in self.out.items():
            sig, subs = fmt(v, prev=True)
            res.append((f"DO{n}", sig, subs))
        return res

    def terms(self, addr):
        """Product terms (signal, coefficient address) of the value stored to D_addr."""
        out = []
        for x in self.wires.get(addr, E):
            if x.startswith("t"):
                sig, c = x[1:].split("*")
                out.append((sig, int(c, 16)))
        return sorted(out, key=lambda t: (t[1], t[0]))


def _ranges(subs):
    """['1F','20','21','30'] -> ['1F-21', '30']"""
    vals = sorted(int(s, 16) for s in subs)
    out, i = [], 0
    while i < len(vals):
        j = i
        while j + 1 < len(vals) and vals[j + 1] == vals[j] + 1:
            j += 1
        out.append(f"{vals[i]:02X}" + (f"-{vals[j]:02X}" if j > i else ""))
        i = j + 1
    return out


def main(argv):
    if not argv or argv[0].startswith("-"):
        print(__doc__)
        return 2
    blocks, _ = dsp_image.parse(open(argv[0], "rb").read())
    f = Flow(blocks)
    rows = f.lines()
    if "--dot" in argv:
        print("digraph tas3108 {\n  rankdir=LR;")
        for node, sig, subs in rows:
            for src in sig.split(", "):
                if src != "-":
                    label = f' [label="{subs}"]' if subs else ""
                    print(f'  "{src}" -> "{node}"{label};')
        print("}")
        return 0
    for node, sig, subs in rows:
        print(f"{node:7s} <- {sig}" + (f"   [{subs}]" if subs else ""))
        if "--terms" in argv and node.startswith("D_"):
            names = {}
            for sa, a, n, kind in dsp_image.regmap(blocks):
                if kind == 1:
                    for k in range(n // 4):
                        names[a + k] = f"s{sa:02X}" + (f".{k}" if n > 4 else "")
            for sig, c in f.terms(int(node[2:], 16)):
                print(f"          {sig} * C_{c:03X}" + (f"<{names[c]}>" if c in names else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
