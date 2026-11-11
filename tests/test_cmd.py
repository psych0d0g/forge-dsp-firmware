"""Protocol port (src/cmd) natively for every flavour.

Expected answers are derived from libs/cmd/v20 of the BASCOM firmware 1.11.6000.
Build first:  make -C tests/native   (or set NATIVE_DIR)
"""
import glob
import os
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.environ.get("NATIVE_DIR", os.path.join(HERE, "native", "build"))
FLAVOURS = sorted(os.path.basename(p) for p in glob.glob(os.path.join(HERE, "..", "src", "flavours", "*")))

INFO = {  # model, sample rate, ad pairs, spdif pairs, out pairs, presets (preference.bas)
    "V30-96LN": ("96LN", "3.0", "FAG", 8), "V30-96ELN": ("96ELN", "4.0", "BNS", 4),
    "V40-96ELN": ("96ELN", "4.0", "BNS", 4), "V40-96ELN-3": ("96ELN3", "4.0", "BNS", 4),
    "V40-CORED8": ("CORED8", "4.0", "BNS", 4), "V41-96ELN-3": ("96ELN3", "4.1", "BNS", 4),
    "V41-CORED8": ("CORED8", "4.1", "BNS", 4),
}


def run(flavour, lines):
    exe = os.path.join(NATIVE, f"cmd-{flavour}")
    p = subprocess.run([exe], input="\n".join(lines) + "\n", capture_output=True, text=True, timeout=10)
    if p.returncode != 0:
        raise AssertionError(p.stderr)
    blocks = p.stdout.split("--\n")
    return [b.strip("\n").split("\n") if b.strip("\n") else [] for b in blocks]


def available(flavour):
    return os.access(os.path.join(NATIVE, f"cmd-{flavour}"), os.X_OK)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class CmdTest(unittest.TestCase):
    def check(self, line, expected, flavour="V41-96ELN-3", pre=()):
        out = run(flavour, list(pre) + [line])
        n = len([p for p in pre if not p.startswith("#")])  # "#..." are harness settings
        self.assertEqual(out[n], expected, line)

    def test_all_flavours_identify(self):
        for f in FLAVOURS:
            model, board, vendor, presets = INFO[f]
            out = run(f, ["1|get.1 system.key.info", "1|get.1 system.info",
                          "1|get.1 system.key.info.board", "1|get.1 system.key.info.type"])
            self.assertEqual(out[0], [f'@0: <.1 system.key.info="{vendor}-{model}-{board}-ABCDEF0102030405'
                                      f'-CCBYNCSA-20990101-MDD"\\r\\n'], f)
            self.assertEqual(out[1], [f'@0: <.1 system.info=["{model}",96000,3,1,4,{presets},32]\\r\\n'], f)
            self.assertEqual(out[2], [f'@0: <.1 system.key.info.board="{board}"\\r\\n'], f)
            self.assertEqual(out[3], [f'@0: <.1 system.key.info.type="{model}"\\r\\n'], f)

    def test_firmware(self):
        self.check("1|get.1 system.firmware", ['@0: <.1 system.firmware="1.11.6000"\\r\\n'])
        self.check("1|get.1 system.firmware.port", ['@0: <.1 system.firmware.port="9.8.7-test"\\r\\n'])
        self.check("1|get.1 system.firmware.x", ['@0: !.1 system.firmware.x unknown command\\r\\n'])

    def test_short_and_long_forms(self):
        for line in ("1|get.1 system.ping", "1|< system.ping", "1|<.1 system.ping", "1|g.1 system.ping"):
            self.check(line, ['@0: <.1 system.ping="pong"\\r\\n'])

    def test_missing_space_is_target(self):
        # "<system..." parses 's' as target slave: forwarded down, receiver digit -1 hits 'p'
        self.check("1|<system.ping", ["@3:<system.oing\\r\\n"])

    def test_other_device_and_broadcast(self):
        self.check("1|get.2 system.ping", ["@3:get.1 system.ping\\r\\n"])
        self.check("1|get.255 system.ping", ['@0: <.1 system.ping="pong"\\r\\n',
                                             "@3:get.255 system.ping\\r\\n"])

    def test_slave_answers_go_up(self):
        self.check('4| <.1 system.ping="pong"', ['@0: <.2 system.ping="pong"\\r\\n'])
        # without DTR on USB and IR master offline: to the RS485 master
        self.check('4| <.1 system.ping="pong"', ['@1: <.2 system.ping="pong"\\r\\n'], pre=["#dtr=0"])

    def test_answers_on_the_requesting_channel(self):
        self.check("3|get.1 system.ping", ['@1: <.1 system.ping="pong"\\r\\n'])
        self.check("2|get.1 system.ping", ['@2: <.1 system.ping="pong"\\r\\n'])

    def test_errors(self):
        self.check("1|get.1 system.foo", ["@0: !.1 system.foo unknown command\\r\\n"])
        self.check("1|get.1 nothing", ["@0: !.1 nothing unknown command\\r\\n"])
        self.check("1|get.1 system.0", ["@0: !.1 system.0 command out of range\\r\\n"])
        self.check("1|set.1 system.boot=1;00", ["@0: !.1 system.boot chksum\\r\\n"])
        # no valid command char: receiver stays 0 ("nothing") -> BASCOM forwards the line
        # to the slave port with the receiver digit decremented
        self.check("1|x.1 system.ping", ["@3:x.0 system.ping\\r\\n"])

    def test_echo_and_values(self):
        self.check("1|get.1 system.echo=hello world", ['@0: <.1 system.echo="hello world"\\r\\n'])

    def test_boot_with_checksum(self):
        body = "system.boot=1"
        cs = "%02X" % (sum(body.encode()) & 0xFF)  # as the GUI (AbstractMessage.convertFields2Raw)
        out = run("V41-96ELN-3", [f"1|set.1 {body};{cs}"])
        self.assertEqual(out[0], ["@0: *.1 system.boot ok\\r\\n", "@reset"])


if __name__ == "__main__":
    unittest.main()
