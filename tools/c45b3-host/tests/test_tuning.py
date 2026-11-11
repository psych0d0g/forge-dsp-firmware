"""Tuning export/compare/restore (#30) against a fake running firmware."""
import re
import unittest

from c45b3 import tuning


class FakeForge:
    """Answers the read commands of tuning.QUERIES from a dict and applies set commands."""

    def __init__(self, values, model="96ELN"):
        self.values, self.model, self.out, self.sets = dict(values), model, b"", []
        self.baudrate = 38400

    def reset_input_buffer(self):
        self.out = b""

    def write(self, data):
        line = data.decode().strip()
        if line == "get.1 system.key.info.type":
            self.out += f' <.1 system.key.info.type="{self.model}"\r\n'.encode()
        elif line.startswith("get.1 "):
            pat = re.escape(line[6:]).replace(r"\*", r"[0-9.]+")
            hits = [k for k in self.values if re.fullmatch(pat, k)]
            if not hits and line.endswith("delay.in.*"):
                self.out += b" !.1 delay.in unknown command\r\n"
            for k in hits:
                self.out += f" <.1 {k}={self.values[k]}\r\n".encode()
        elif line.startswith("set.1 "):
            body, cs = line[6:].split(";")
            assert cs == tuning.checksum(body), line
            reg, val = body.split("=", 1)
            self.sets.append(body)
            self.values[reg[:-5] if reg.endswith(".calc") else reg] = val
            self.out += f" *.1 {reg} ok\r\n".encode()

    def read(self, n):
        d, self.out = self.out[:n], self.out[n:]
        return d


DEVICE = {"mixer.position": "1", "mixer.1.1.1": "50", "gain.out.1": "200", "gain.out.2": "255",
          "mute.out.1": "0", "phase.out.1": "1", "delay.1": "120", "system.channel.name.1": '"Sub"',
          "system.channel.name.2": '""', "extender.ir.custom.1": "[0,4660,22136]",
          "modc.9": "[1,108,01DF280F,70,0,0,0,5000A00F]", "biquad.1.1": "[1,1000,35,1414,0,128]",
          "biquad.1.2": "[0,0,0,0,0,0]", "biquad.1.3": "[255,65535,-1,65535,65535,255]"}


class TuningTest(unittest.TestCase):
    def app(self, dev):
        return tuning.App(dev, log=lambda m: None)

    def test_export_cfg_roundtrip(self):
        model, entries = tuning.export(self.app(FakeForge(DEVICE)))
        text = tuning.to_cfg(model, entries, ["test"])
        self.assertTrue(text.startswith("; test\n[96ELN]\n"))
        self.assertIn('FILE_v10 = "biquad.1.1.calc=[1,1000,35,1414,0,128]"', text)
        self.assertIn('FILE_v10 = "system.channel.name.1="Sub""', text)
        self.assertNotIn("gain.out.2", text)            # never written (0xFF)
        self.assertNotIn("biquad.1.3", text)
        self.assertNotIn("channel.name.2", text)        # empty
        m2, e2 = tuning.parse_cfg(text)
        self.assertEqual(m2, "96ELN")
        self.assertEqual(tuning.compare(e2, entries), [])

    def test_restore_writes_only_differences(self):
        model, entries = tuning.export(self.app(FakeForge(DEVICE)))
        changed = dict(DEVICE, **{"gain.out.1": "100", "biquad.1.1": "[1,500,40,1414,0,128]",
                                  "biquad.1.2": "[0,10,0,1,0,0]"})   # bypass, clamped by the firmware
        dev = FakeForge(changed)
        written, errors = tuning.restore(self.app(dev), model, entries, log=lambda m: None)
        self.assertEqual((written, errors), (2, 0))
        self.assertEqual(sorted(dev.sets), ["biquad.1.1.calc=[1,1000,35,1414,0,128]", "gain.out.1=200"])

    def test_wrong_model_is_refused(self):
        model, entries = tuning.export(self.app(FakeForge(DEVICE)))
        dev = FakeForge(DEVICE, model="96ELN3")
        with self.assertRaises(SystemExit):
            tuning.restore(self.app(dev), model, entries, log=lambda m: None)
        self.assertEqual(dev.sets, [])

    def test_gui_file(self):
        text = '[CORED8]\ncount = 2\nFILE_v10 = "gain.out.1=111"\nFILE_v10 = "mute.out.3=1"\n'
        self.assertEqual(tuning.parse_cfg(text), ("CORED8", [("gain.out.1", "111"), ("mute.out.3", "1")]))


if __name__ == "__main__":
    unittest.main()
