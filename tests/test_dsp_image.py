"""#43: the TAS3108 boot images in all flavours parse with valid checksums and rebuild bit identical."""
import glob
import os
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


if __name__ == "__main__":
    unittest.main()
