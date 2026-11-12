"""#43: the TAS3108 boot images in all flavours parse with valid checksums and rebuild bit identical."""
import glob
import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import dsp_image  # noqa: E402


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


if __name__ == "__main__":
    unittest.main()
