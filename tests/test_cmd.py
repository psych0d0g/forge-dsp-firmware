"""Protocol port (src/cmd) natively for every flavour.

Expected answers are derived from libs/cmd/v20 of the BASCOM firmware 1.11.6000.
Build first:  make -C tests/native   (or set NATIVE_DIR)
"""
import glob
import os
import pathlib
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


def run(flavour, lines, eeprom=None, want_eeprom=False):
    """eeprom: bytes of the internal EEPROM (default blank 0xFF -> sysinit)."""
    import tempfile
    exe = os.path.join(NATIVE, f"cmd-{flavour}")
    with tempfile.TemporaryDirectory() as d:
        args = [exe]
        if eeprom is not None or want_eeprom:
            fin = os.path.join(d, "in.bin")
            pathlib.Path(fin).write_bytes(eeprom if eeprom is not None else b"\xff" * 4096)
            args += [fin, os.path.join(d, "out.bin")]
        p = subprocess.run(args, input="\n".join(lines) + "\n", capture_output=True, text=True, timeout=10)
        out_ee = pathlib.Path(d, "out.bin").read_bytes() if len(args) > 2 else None
    if want_eeprom:
        return p, out_ee
    return parse(p)


def parse(p):
    if p.returncode != 0:
        raise AssertionError(p.stderr)
    blocks = p.stdout.split("--\n")
    return [b.strip("\n").split("\n") if b.strip("\n") else [] for b in blocks]


def layout(flavour):
    """ERAM_<NAME> -> (address, count, size) from the generated flavour_eram.h"""
    import re
    t = pathlib.Path(HERE, "..", "src", "flavours", flavour, "flavour_eram.h").read_text()
    out = {}
    for name, a in re.findall(r"#define (ERAM_\w+?) (\d+)  /\*", t):
        n = int(re.search(rf"#define {name}_COUNT (\d+)", t).group(1))
        sz = int(re.search(rf"#define {name}_SIZE (\d+)", t).group(1))
        out[name[5:].lower()] = (int(a), n, sz)
    return out


def image(flavour, version="1.11.6000", state=0x55, names=None, cycles=7, errors=()):
    """EEPROM as the BASCOM firmware leaves it after a completed sysinit."""
    L = layout(flavour)
    ee = bytearray(b"\xff" * 4096)
    a, _, sz = L["version_register"]
    ee[a:a + len(version) + 1] = version.encode() + b"\0"
    a = L["registers_eram"][0]
    ee[a:a + 11] = bytes([state, 0, 0, 8, 32, 16, 32, 32, 0, 0, 0])
    a = L["device_bootcycles_eram"][0]
    ee[a:a + 2] = cycles.to_bytes(2, "little")
    a, n, sz = L["channel_names"]
    for i in range(n):
        nm = (names or {}).get(i + 1, "").encode()
        ee[a + i * sz:a + i * sz + len(nm) + 1] = nm + b"\0"
    ta = L["error_count_type_eram"][0]
    ca = L["error_count_eram"][0]
    for i, (t, c) in enumerate(errors):
        ee[ta + i] = t
        ee[ca + 2 * i:ca + 2 * i + 2] = c.to_bytes(2, "little")
    return bytes(ee)


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


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class EepromTest(unittest.TestCase):
    """EEPROM layout of the BASCOM firmware: existing settings must survive."""

    def test_existing_settings_are_kept(self):
        for f in FLAVOURS:
            ee = image(f, names={1: "Front L", 8: "Sub"}, cycles=41, errors=[(3, 5), (130, 1)])
            p, out = run(f, ["1|get.1 system.channel.name.1", "1|get.1 system.channel.name.8",
                             "1|get.1 system.cycles", "1|get.1 system.errors"], eeprom=ee, want_eeprom=True)
            r = parse(p)
            self.assertEqual(r[0], ['@0: <.1 system.channel.name.1="Front L"\\r\\n'], f)
            self.assertEqual(r[1], ['@0: <.1 system.channel.name.8="Sub"\\r\\n'], f)
            self.assertEqual(r[2], ["@0: <.1 system.cycles=42\\r\\n"], f)
            self.assertEqual(r[3], ["@0: <.1 system.errors=[[3,5],[130,1]]\\r\\n"], f)
            a = layout(f)["device_bootcycles_eram"][0]
            diff = [i for i in range(4096) if out[i] != ee[i]]
            self.assertEqual(diff, [a], f"{f}: only the boot counter may change")

    def test_other_version_triggers_sysinit(self):
        ee = image("V41-96ELN-3", version="1.10.5000")
        p, out = run("V41-96ELN-3", [], eeprom=ee, want_eeprom=True)
        self.assertIn("@reset", p.stdout)
        a = layout("V41-96ELN-3")["registers_eram"][0]
        self.assertEqual(out[a], 0xFF)  # startup_state_sysinit, everything else untouched
        self.assertEqual([i for i in range(4096) if out[i] != ee[i]], [a])

    def test_blank_eeprom_sysinit(self):
        for f in FLAVOURS:
            p, out = run(f, ["1|get.1 system.cycles"], want_eeprom=True)
            L = layout(f)
            a = L["version_register"][0]
            self.assertEqual(out[a:a + 10], b"1.11.6000\0", f)
            a = L["registers_eram"][0]
            self.assertEqual(out[a:a + 4], bytes([0xFF, 0, 0, 8]), f)  # stays sysinit (PORT)
            a, n, sz = L["channel_names"]
            self.assertTrue(all(out[a + i * sz] == 0 for i in range(n)), f)
            self.assertEqual(parse(p)[0], ["@0: <.1 system.cycles=1\\r\\n"], f)

    def test_channel_name_write(self):
        f = "V40-CORED8"
        p, out = run(f, ['1|set.1 system.channel.name.2="A very long channel name"',
                         "1|set.1 system.channel.name.2=noquotes",
                         '1|set.1 system.channel.name.9="x"'], eeprom=image(f), want_eeprom=True)
        r = parse(p)
        self.assertEqual(r[0], ["@0: *.1 system.channel.name.2 ok\\r\\n"])
        self.assertEqual(r[1], ["@0: !.1 system.channel.name.2 unknown para\\r\\n"])
        self.assertEqual(r[2], ["@0: !.1 system.channel.name.9 command out of range\\r\\n"])
        a, _, sz = layout(f)["channel_names"]
        self.assertEqual(out[a + sz:a + 2 * sz], b"A very long chan\0")  # 16 chars + NUL

    def test_baud_and_debug_registers(self):
        f = "V30-96LN"
        p, out = run(f, ["1|set.1 system.baud.1=96", "1|set.1 system.baud.3=8",
                         "1|set.1 system.debug.mode=5", "1|set.1 system.debug.mode=9",
                         "1|get.1 system.debug.mode"], eeprom=image(f), want_eeprom=True)
        r = parse(p)
        self.assertEqual(r[0], ["@0: !.1 system.baud.1 unknown para\\r\\n"])
        self.assertEqual(r[1], ["@0: *.1 system.baud.3 ok\\r\\n"])
        self.assertEqual(r[3], ["@0: !.1 system.debug.mode command out of range\\r\\n"])
        self.assertEqual(r[4], ["@0: <.1 system.debug.mode=5\\r\\n"])
        a = layout(f)["registers_eram"][0]
        self.assertEqual(out[a + 6], 8)   # reg_baud_ch3 = register 7
        self.assertEqual(out[a + 1], 5)   # reg_debugmode = register 2


if __name__ == "__main__":
    unittest.main()
