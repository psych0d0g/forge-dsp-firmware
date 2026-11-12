"""#49: analysis functions of tools/measure/forge_measure.py on synthetic recordings."""
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "measure"))

try:
    import numpy as np
    import scipy  # noqa: F401
    HAVE = True
except ImportError:
    HAVE = False


@unittest.skipUnless(HAVE, "numpy/scipy not installed")
class MeasureTest(unittest.TestCase):
    def setUp(self):
        import forge_measure
        self.fm = forge_measure
        self.dir = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.dir.cleanup()

    def wav(self, name, x, rate=96000):
        p = os.path.join(self.dir.name, name)
        self.fm.write_wav(p, x, rate)
        return p

    def test_level(self):
        p = self.wav("t.wav", self.fm.tone(1000, -20, 4, 96000))
        r = self.fm.level(p, 1000)
        self.assertAlmostEqual(r["tone_dbfs"], -20, delta=0.2)
        self.assertAlmostEqual(r["rms_dbfs"], -23, delta=0.2)

    def test_response(self):
        fr = self.fm.multitone_freqs(8)
        t = np.arange(5 * 96000) / 96000
        gain = np.where(np.arange(len(fr)) == 3, 2.0, 1.0)  # tone 4 +6 dB
        a = sum(np.sin(2 * np.pi * f * t) for f in fr) * 10 ** (-30 / 20)
        b = sum(g * np.sin(2 * np.pi * f * t) for f, g in zip(fr, gain)) * 10 ** (-30 / 20)
        r = self.fm.response(self.wav("a.wav", a), self.wav("b.wav", b), fr)
        for i, (f, d) in enumerate(r):
            self.assertAlmostEqual(d, 6.02 if i == 3 else 0.0, delta=0.1, msg=f)

    def test_phase(self):
        rate = 96000
        fr = self.fm.multitone_freqs(6)
        t = np.arange(4 * rate) / rate
        sh = [-30.0 * i for i in range(len(fr))]                     # left lags by 0, 30, 60 ... deg
        ref = sum(np.sin(2 * np.pi * f * t) for f in fr) * 0.01
        lag = sum(np.sin(2 * np.pi * f * t + np.radians(d)) for f, d in zip(fr, sh)) * 0.01
        a = self.wav("a.wav", ref)                                   # both channels equal
        p = os.path.join(self.dir.name, "b.wav")
        import wave
        d = (np.stack([lag, ref], 1) * (2 ** 31 - 1)).astype("<i4")
        with wave.open(p, "wb") as w:
            w.setnchannels(2); w.setsampwidth(4); w.setframerate(rate); w.writeframes(d.tobytes())
        for (f, got), want in zip(self.fm.phase(a, p, fr), sh):
            self.assertAlmostEqual(got, (want + 180) % 360 - 180, delta=0.5, msg=f)

    def test_clicks_and_dropouts(self):
        rate = 96000
        x = self.fm.tone(1000, -20, 10, rate)        # window after 6.5 s ends before the fade-out
        x[int(3 * rate)] += 0.5                         # click at 3 s
        x[int(5 * rate):int(5.2 * rate)] = 0            # 200 ms dropout at 5 s
        r = self.fm.clicks(self.wav("c.wav", x, rate), [3.0, 6.5])
        self.assertTrue(any(abs(t - 5.0) < 0.01 and ms >= 190 for t, ms in r["dropouts"]), r["dropouts"])
        ev = {e: n for e, n, _ in r["events"]}
        self.assertGreater(ev[3.0], 0)                  # click found
        self.assertEqual(ev[6.5], 0)                    # clean span


if __name__ == "__main__":
    unittest.main()
