"""#43: the TAS3108 boot images in all flavours parse with valid checksums and rebuild bit identical."""
import glob
import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import dsp_image  # noqa: E402
import tas3108_dis  # noqa: E402
import tas3108_flow  # noqa: E402
import c51_dis  # noqa: E402
import tas3108_asm  # noqa: E402
import tas3108_sim  # noqa: E402
import tas3108_opt  # noqa: E402


class DspImageTest(unittest.TestCase):
    def test_all_flavours(self):
        headers = sorted(glob.glob(os.path.join(HERE, "..", "src", "flavours", "*", "flavour_data.h")))
        self.assertTrue(headers)
        n = 0
        for h in headers:
            for p in (1, 2):
                img = dsp_image.extract(h, p)
                blocks, rest, errors = dsp_image.check(img)
                self.assertEqual(errors, [], (h, p))
                self.assertTrue(blocks[-1].terminator, (h, p))
                self.assertEqual(rest, bytes(8), (h, p))
                mems = {b.mem for b in blocks if not b.terminator}
                self.assertTrue({0, 1, 2, 3, 4} <= mems, (h, p))
                n += 1
        self.assertEqual(n, 2 * len(headers))

    def test_register_tables(self):
        """Every register the MCU firmware writes (biquads, input and output volume) is a
        coefficient entry of the matching length in the subaddress table of the program the
        core runs (core 1: dsp_1_code, cores 2 and 3: dsp_2_code)."""
        headers = sorted(glob.glob(os.path.join(HERE, "..", "src", "flavours", "*", "flavour_data.h")))
        for h in headers:
            t = open(h).read()
            regs = {p: dsp_image.regmap(dsp_image.parse(dsp_image.extract(h, p))[0]) for p in (1, 2)}

            def table(name):
                m = re.search(r"BAS_DATA_TABLE\(%s, \d+\) = \{(.*?)\};" % name, t, re.S)
                return [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1))] if m else []

            def check(core, reg, length, what):
                s, addr, n, kind = regs[1 if core == 1 else 2][reg]
                self.assertEqual((kind, n), (1, length), (h, what, core, hex(reg)))

            n = 0
            for core, reg in zip(table("data_flow_dsp_bq_core")[1:], table("data_flow_dsp_bq_reg")[1:]):
                check(core, reg, 20, "biquad")
                n += 1
            self.assertGreater(n, 300, h)
            for reg in table("data_flow_dsp_input_vol_reg")[1:]:
                check(1, reg, 4, "input volume")
            for core, reg in zip(table("data_flow_dsp_output_vol_dsp")[1:], table("data_flow_dsp_output_vol_reg")[1:]):
                check(core, reg, 4, "output volume")

    def test_disassembler(self):
        """All DSP programs decode with the SLEU067A tables; the only unknown field is ALU2 0x4F.
        Every program starts with JMP to the SAP block (ST(DI,DATA,0..7), outputs DO1..DO8)."""
        headers = sorted(glob.glob(os.path.join(HERE, "..", "src", "flavours", "*", "flavour_data.h")))
        for h in headers:
            for p in (1, 2):
                blocks = dsp_image.parse(dsp_image.extract(h, p))[0]
                prog = tas3108_dis.program(blocks)
                lines, unknown = [], set()
                for w in prog:
                    text, u = tas3108_dis.disasm(w)
                    lines.append(text)
                    unknown.update(u)
                self.assertLessEqual(unknown, {"ALU2=0x4f"}, (h, p))
                self.assertEqual(lines[0], "JMP | NOP | NOP | NOP | PCADDR(0x00c)", (h, p))
                block = "\n".join(lines[0x0C:0x16])
                for n in range(8):
                    self.assertIn(f"ST(DI,DATA,D_{n:03X})", block, (h, p))
                    self.assertIn(f"CLP32,DO{n + 1})", block, (h, p))

    def test_flow(self):
        """The signal flow analysis reaches every biquad, volume and inversion register the MCU
        firmware writes. V40-96ELN program 2: four chains in1..in4 -> delay -> output volume."""
        headers = sorted(glob.glob(os.path.join(HERE, "..", "src", "flavours", "*", "flavour_data.h")))
        for h in headers:
            t = open(h).read()

            def table(name):
                m = re.search(r"BAS_DATA_TABLE\(%s, \d+\) = \{(.*?)\};" % name, t, re.S)
                return [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1))][1:] if m else []

            want = {1: set(), 2: set()}
            for core, reg in zip(table("data_flow_dsp_bq_core"), table("data_flow_dsp_bq_reg")):
                want[1 if core == 1 else 2].add(f"s{reg:02X}")
            want[1] |= {f"s{r:02X}" for r in table("data_flow_dsp_input_vol_reg")}
            for kind in ("output_vol", "output_inv"):
                for core, reg in zip(table(f"data_flow_dsp_{kind}_dsp"), table(f"data_flow_dsp_{kind}_reg")):
                    want[1 if core == 1 else 2].add(f"s{reg:02X}")
            for p in (1, 2):
                f = tas3108_flow.Flow(dsp_image.parse(dsp_image.extract(h, p))[0])
                self.assertEqual(want[p] - f.subaddresses(), set(), (h, p))
        h = os.path.join(HERE, "..", "src", "flavours", "V40-96ELN", "flavour_data.h")
        rows = tas3108_flow.Flow(dsp_image.parse(dsp_image.extract(h, 2))[0]).lines()
        self.assertIn(("D_01C", "in1", "1F-28"), rows)
        self.assertIn(("D_1BE", "dly16", "FA"), rows)
        self.assertIn(("DO1", "~D_1BE", ""), rows)

    def test_mixer_commands(self):
        """mixer.C.G of the MCU firmware (data_flow_mix_cmd2array -> storage index b, written in
        4-word registers from data_flow_mix_dsp_reg) addresses one mixer output per channel C and
        one signal per gain G in the DSP program of core 1. i2c_wbuffer sends the buffer
        backwards, so word w of a register lands at coefficient word 3 - w."""
        headers = sorted(glob.glob(os.path.join(HERE, "..", "src", "flavours", "*", "flavour_data.h")))
        for h in headers:
            t = open(h).read()

            def table(name):
                m = re.search(r"BAS_DATA_TABLE\(%s, \d+\) = \{(.*?)\};" % name, t, re.S)
                return [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1))]

            blocks = dsp_image.parse(dsp_image.extract(h, 1))[0]
            regs = dsp_image.regmap(blocks)
            mixregs = table("data_flow_mix_dsp_reg")[1:]
            f = tas3108_flow.Flow(blocks)
            use = {}  # coefficient address -> (output word, signal)
            for a in f.wires:
                for sig, c in f.terms(a):
                    use.setdefault(c, set()).add((a, sig))
            c2a = table("data_flow_mix_cmd2array")
            per_c, per_g = {}, {}
            for i, b in enumerate(c2a[1:]):
                if b in (0, 0xFF):
                    continue
                c, g = i // 10 + 1, i % 10 + 1
                w = b - 1
                coef = regs[mixregs[w // 4]][1] + 3 - w % 4
                self.assertIn(coef, use, (h, c, g))
                self.assertEqual(len(use[coef]), 1, (h, c, g))
                out, sig = next(iter(use[coef]))
                if g <= 8:
                    per_c.setdefault(c, set()).add(out)
                    per_g.setdefault(g, set()).add(sig)
            for c, outs in per_c.items():
                self.assertEqual(len(outs), 1, (h, c, outs))
            for g, sigs in per_g.items():
                self.assertEqual(len(sigs), 1, (h, g, sigs))
            self.assertGreaterEqual(len(per_c), 8, h)

    def test_8051(self):
        """The 8051 program dispatches the subaddress kinds through a table of read/write handlers;
        all handlers and the code reachable from them and the vectors decode without invalid
        opcodes. Unused subaddresses (kind 0) read through the default handler."""
        headers = sorted(glob.glob(os.path.join(HERE, "..", "src", "flavours", "*", "flavour_data.h")))
        for h in headers:
            for p in (1, 2):
                m = dsp_image.c51(dsp_image.parse(dsp_image.extract(h, p))[0])
                self.assertIsNotNone(c51_dis.handler_table(m), (h, p))
                kinds = {k for _, _, _, k in dsp_image.regmap(dsp_image.parse(dsp_image.extract(h, p))[0])}
                hs = c51_dis.handlers(m, kinds)
                entries = [v for v in c51_dis.VECTORS if m[v]] + [a for rw in hs.values() for a in rw]
                for a in entries:
                    self.assertLess(a, 0x2B00, (h, p, hex(a)))
                code, _ = c51_dis.disassemble(m, entries)
                bad = [hex(pc) for pc, (_, text) in code.items() if text.startswith("DB")]
                self.assertEqual(bad, [], (h, p))

    def test_assembler_roundtrip(self):
        """#47: disassembly (with register names) -> assembler -> the same words and a bit
        identical boot image, for all programs; the originals pass the hardware rules."""
        headers = sorted(glob.glob(os.path.join(HERE, "..", "src", "flavours", "*", "flavour_data.h")))
        for h in headers:
            for p in (1, 2):
                img = dsp_image.extract(h, p)
                blocks, rest = dsp_image.parse(img)
                prog = tas3108_dis.program(blocks)
                names = tas3108_dis.coef_names(dsp_image.regmap(blocks))
                src = "\n".join(f"{pc:04X}  {w:014X}  {tas3108_dis.disasm(w, names)[0]}"
                                for pc, w in enumerate(prog))
                words = tas3108_asm.assemble(src)
                self.assertEqual(words, prog, (h, p))
                self.assertEqual(dsp_image.build(tas3108_asm.replace_program(blocks, words), rest), img, (h, p))
                self.assertEqual(tas3108_asm.check(prog), [], (h, p))

    def test_checker(self):
        """The checker finds broken hardware rules; labels work."""
        nop = "NOP | NOP | NOP | NOP | NOP"
        ok = tas3108_asm.assemble("\n".join([
            "JMP | NOP | NOP | NOP | PCADDR(start)", nop, nop, nop,
            "end:", "STOP | NOP | NOP | NOP | PCADDR(0)", nop, nop, nop,
            "start:", "NOP | ADD(ACC,LR,NONE,DLYI) | NOP | NOP | DLYPTR(3)"] + [nop] * 13 + [
            "NOP | NOP | NOP | NOP | ST(DLYO,DATA,D_010)",
            "JMP | NOP | NOP | NOP | PCADDR(end)", nop, nop, nop]))
        self.assertEqual(ok[0] & 0x3FF, 8)
        self.assertEqual(tas3108_asm.check(ok), [])
        short = list(ok)
        del short[9]  # DLYPTR -> ST(DLYO) only 13 instructions
        self.assertTrue(any("DLYO" in e for e in tas3108_asm.check(short)))
        jmp = tas3108_asm.assemble("JMP | NOP | NOP | NOP | PCADDR(0x004)")[0]
        far = ok[:-4] + [0] * (0x400 - len(ok) + 4) + [jmp, 0, 0, 0]  # last JMP at 0x400
        self.assertTrue(any("1K" in e for e in tas3108_asm.check(far)))
        busy = list(ok)
        busy[1] = tas3108_asm.assemble("NOP | CLRACC | NOP | NOP | NOP")[0]
        self.assertTrue(any("NOPs" in e for e in tas3108_asm.check(busy)))
        long = ok[:8] + [0] * 1500 + ok[8:]
        long[0] = tas3108_asm.assemble("JMP | NOP | NOP | NOP | PCADDR(0x0C)")[0]
        self.assertTrue(any("budget" in e for e in tas3108_asm.check(long)))
        with self.assertRaises(tas3108_asm.AsmError):
            tas3108_asm.assemble("NOP | FOO | NOP | NOP | NOP")

    def test_simulator(self):
        """#47: the equivalence check finds changed programs (swapped MACs, a changed coefficient
        address, ST(DLYO) too early, a dropped MAC in the level meter) and the volume blocks
        produce the register value as gain (C_007 = 0.99988 trim)."""
        import random
        h = os.path.join(HERE, "..", "src", "flavours", "V40-96ELN", "flavour_data.h")
        for p in (1, 2):
            img = dsp_image.extract(h, p)
            blocks, rest = dsp_image.parse(img)
            prog = tas3108_dis.program(blocks)
            self.assertIsNone(tas3108_sim.compare(img, img, frames=2), p)
            names = tas3108_dis.coef_names(dsp_image.regmap(blocks))
            i = next(pc for pc, w in enumerate(prog) if "b2>" in tas3108_dis.disasm(w, names)[0])
            j = next(pc for pc, w in enumerate(prog) if tas3108_dis.fields(w)[6] == 4)
            k = max(pc for pc, w in enumerate(prog) if "ADD(ACC,MR,NONE,ACC)" in tas3108_dis.disasm(w)[0])
            muts = []
            w = list(prog); w[i + 1], w[i + 2] = w[i + 2], w[i + 1]; muts.append(w)  # noqa: E702
            w = list(prog); w[i + 3] ^= 1 << 14; muts.append(w)  # noqa: E702
            w = list(prog); w[j - 1], w[j] = w[j], w[j - 1]; muts.append(w)  # noqa: E702
            w = list(prog); w[k] = 0; muts.append(w)  # noqa: E702
            for n, w in enumerate(muts):
                other = dsp_image.build(tas3108_asm.replace_program(blocks, w), rest)
                self.assertIsNotNone(tas3108_sim.compare(img, other, frames=2), (p, n))
        blocks = dsp_image.parse(dsp_image.extract(h, 2))[0]
        coef, data = tas3108_sim.initial_memory(blocks, random.Random(1))
        core = tas3108_sim.Core(tas3108_dis.program(blocks), coef, data)
        for _ in range(3000):  # four ramps with alpha = 1/128
            core.frame([0] * 8)
        self.assertAlmostEqual(core.coef[0x005] / coef[0x3D1], 0.99988, delta=0.0005)

    def test_optimised_programs(self):
        """#47: delay waits filled (program 1: volume blocks, program 2: next chain) for all
        96ELN/CORED8 flavours: rules ok, fewer cycles, bit identical outputs and memory."""
        headers = sorted(glob.glob(os.path.join(HERE, "..", "src", "flavours", "*", "flavour_data.h")))
        for h in headers:
            if "96LN" in h:
                continue  # other structure (waits partly used already)
            for p in (1, 2):
                img = dsp_image.extract(h, p)
                blocks, rest = dsp_image.parse(img)
                prog = tas3108_dis.program(blocks)
                if p == 1:
                    new = tas3108_opt.interleave_delays(prog)
                else:
                    new = tas3108_opt.fill_waits_forward(prog, dsp_image.regmap(blocks))
                self.assertEqual(tas3108_asm.check(new), [], (h, p))
                self.assertLess(tas3108_asm.cycles(new), tas3108_asm.cycles(prog) - (100 if p == 1 else 30), (h, p))
                other = dsp_image.build(tas3108_asm.replace_program(blocks, new), rest)
                self.assertIsNone(tas3108_sim.compare(img, other, frames=6), (h, p))


if __name__ == "__main__":
    unittest.main()


class OptHeaderTest(unittest.TestCase):
    """#47: dsp_opt.h holds the optimized programs; they pass the rules and differ from the original."""

    def test_headers(self):
        import re
        sys.path.insert(0, os.path.join(HERE, "..", "tools"))
        import dsp_image as di
        import tas3108_asm as asm
        import tas3108_dis as dis
        for f in ("V40-96ELN", "V40-96ELN-3", "V30-96ELN"):
            base = os.path.join(HERE, "..", "src", "flavours", f)
            t = open(os.path.join(base, "dsp_opt.h")).read()
            for n in (1, 2):
                body = re.search(r"BAS_DATA_TABLE\(dsp_%d_code_opt, \d+\) = \{(.*?)\};" % n, t, re.S).group(1)
                b = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", body))
                img = b[2:2 + (b[0] | b[1] << 8)]
                words = dis.program(di.parse(img)[0])
                self.assertEqual(asm.check(words), [], (f, n))
                self.assertLess(asm.cycles(words), asm.cycles(dis.program(di.parse(di.extract(os.path.join(base, "flavour_data.h"), n))[0])), (f, n))
