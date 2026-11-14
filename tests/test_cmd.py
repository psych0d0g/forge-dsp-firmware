"""Protocol port (src/cmd) natively for every flavour.

Expected answers are derived from libs/cmd/v20 of the BASCOM firmware 1.11.6000.
Build first:  make -C tests/native   (or set NATIVE_DIR)
"""
import glob
import math
import os
import pathlib
import re
import subprocess
import sys
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


def consts(flavour, *names):
    import re
    t = pathlib.Path(HERE, "..", "src", "flavours", flavour, "flavour.h").read_text()
    return [int(re.search(rf"#define BAS_{n} (\d+)", t).group(1)) for n in names]


def layout_end(flavour):
    """first EEPROM address after the original layout (ERAM_TOTAL_SIZE)"""
    import re
    t = pathlib.Path(HERE, "..", "src", "flavours", flavour, "flavour_eram.h").read_text()
    return int(re.search(r"#define ERAM_TOTAL_SIZE (\d+)", t).group(1))


def table_words(flavour, label):
    import re
    t = pathlib.Path(HERE, "..", "src", "flavours", flavour, "flavour_data.h").read_text()
    body = re.search(rf"BAS_DATA_TABLE\({label}, \d+\) = \{{(.*?)\}};", t, re.S).group(1)
    b = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", body))
    return [b[i] | b[i + 1] << 8 for i in range(0, len(b), 2)]


def xmodem(data):
    crc = 0
    for c in data:
        crc ^= c << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class StoreTest(unittest.TestCase):
    """Presets in the external 24xx512 (libs/store/cores)."""

    def at24_with_preset(self, flavour, preset, seed):
        import random
        fs, cb = consts(flavour, "STORAGE_FRAMESIZE", "STORAGE_CHKSUM_BASE")
        base = table_words(flavour, "data_store_addr_values")[preset]
        rnd = random.Random(seed)
        frame = bytearray(rnd.randrange(256) for _ in range(fs))
        crc = xmodem(frame[:cb - 1])
        frame[cb - 1:cb + 1] = crc.to_bytes(2, "little")
        img = bytearray(b"\xff" * 65536)
        img[base:base + fs] = frame
        return img, bytes(frame), base

    def run_at24(self, flavour, img, lines, out=False):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            fin = pathlib.Path(d, "at24.bin")
            fin.write_bytes(bytes(img))
            pre = [f"#at24={fin}"] + ([f"#at24out={d}/out.bin"] if out else [])
            p = subprocess.run([os.path.join(NATIVE, f"cmd-{flavour}")], input="\n".join(pre + lines) + "\n",
                               capture_output=True, text=True, timeout=10)
            self.assertEqual(p.returncode, 0, p.stderr)
            o = pathlib.Path(d, "out.bin").read_bytes() if out else None
        return p.stdout, o

    def test_load_values_and_checksum(self):
        for f in FLAVOURS:
            presets, = consts(f, "MODUL_MAX_PRESET")
            p = presets - 1
            img, frame, base = self.at24_with_preset(f, p, seed=len(f))
            out, _ = self.run_at24(f, img, [f"#store_load={p}"])
            self.assertIn(f"@store:{frame.hex().upper()} chksum_error=0", out, f)
            img[base + 5] ^= 0x01
            out, _ = self.run_at24(f, img, [f"#store_load={p}", "1|get.1 system.errors"])
            self.assertIn("chksum_error=1", out, f)
            self.assertIn("system.errors=[[128,1]]", out, f)  # errorcount_fw_data_chksum_soft

    def test_biquad_record_is_read_backwards(self):
        f = "V41-96ELN-3"
        bqtv = table_words(f, "data_store_addr_bqtv")
        img = bytearray(b"\xff" * 65536)
        rec = bytes(range(0x10, 0x1A))
        a = bqtv[1] + 3 * 10
        img[a:a + 10] = rec
        out, _ = self.run_at24(f, img, ["#bq=1,3"])
        self.assertIn(f"@bq:{rec[::-1].hex().upper()} type={rec[9]:02X}", out)

    def test_save_values_page_writes(self):
        for f in ("V41-96ELN-3", "V30-96LN"):
            fs, cb = consts(f, "STORAGE_FRAMESIZE", "STORAGE_CHKSUM_BASE")
            img, frame, base = self.at24_with_preset(f, 0, seed=7)
            target = table_words(f, "data_store_addr_values")[2]
            lines = ["#store_load=0", "#store_set=1,170", "#store_save=2"]
            out, res = self.run_at24(f, img, lines, out=True)
            exp = bytearray(frame)
            exp[0] = 170
            exp[cb - 1:cb + 1] = xmodem(exp[:cb - 1]).to_bytes(2, "little")
            self.assertEqual(res[target:target + fs], bytes(exp), f)
            changed = [i for i in range(65536) if res[i] != img[i]]
            self.assertTrue(all(target <= i < target + fs for i in changed), f)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class ExpanderTest(unittest.TestCase):
    """libs/expander: exact I2C transfers of the BASCOM code."""

    def run_lines(self, flavour, lines):
        p = subprocess.run([os.path.join(NATIVE, f"cmd-{flavour}")], input="\n".join(lines) + "\n",
                           capture_output=True, text=True, timeout=10)
        self.assertEqual(p.returncode, 0, p.stderr)
        return [l for l in p.stdout.splitlines() if l.startswith("@")]

    def test_96eln3_pca9554_41_and_pca9555_4f(self):
        out = self.run_lines("V41-96ELN-3", ["#i2cdev=1,40,0=5A", "#i2cdev=1,4E", "#i2clog=1",
                                             "#expander_init", "#pca9555=00C0,0040", "#i2clog=0",
                                             "#regs=1,4E,8"])
        self.assertEqual(out, [
            "@i2c1: S 40 P", "@i2c1: S 40 03 FF P", "@i2c1: S 40 00 Sr 41 r5AN P",
            "@i2c1: S 4E P", "@i2c1: S 4E 06 3F P", "@i2c1: S 4E 07 FF P",
            "@i2c1: S 4E 02 3F P", "@i2c1: S 4E 03 00 P",
            "@i2c1: S 4E 01 Sr 4F r00N P", "@i2c1: S 4E 00 Sr 4F r00N P",
            "@hw:0061",
            "@i2c1: S 4E 03 Sr 4F r00N P", "@i2c1: S 4E 02 Sr 4F r3FN P",
            "@i2c1: S 4E 02 7F P", "@i2c1: S 4E 03 00 P",
            "@regs:00007F0000003FFF"])

    def test_cored8_falls_back_to_a_addresses(self):
        out = self.run_lines("V40-CORED8", ["#i2cdev=1,70", "#i2cdev=1,7E", "#i2clog=1",
                                            "#expander_init", "#pca9554=80,00"])
        self.assertEqual(out, [
            "@i2c1: S 40 P", "@i2c1: S 70 P", "@i2c1: S 70 03 FF P", "@i2c1: S 70 00 Sr 71 r00N P",
            "@i2c1: S 4E P", "@i2c1: S 7E P", "@i2c1: S 7E 03 3F P", "@i2c1: S 7E 01 BF P",
            "@i2c1: S 7E 00 Sr 7F r00N P", "@hw:0061",
            "@i2c1: S 7E 01 Sr 7F rBFN P", "@i2c1: S 7E 01 3F P"])

    def test_missing_expander_is_counted(self):
        out = self.run_lines("V40-96ELN", ["#expander_init", "1|get.1 system.errors"])
        # pca9554_41_rdy = 6, pca9554_4F_rdy = 5, errorcount_offset_other = 17
        self.assertIn("@hw:0001", out)
        self.assertIn("@0: <.1 system.errors=[[23,1],[22,1]]\\r\\n", out)

    def test_96ln_has_no_expander(self):
        out = self.run_lines("V30-96LN", ["#i2clog=1", "#expander_init"])
        self.assertEqual(out, ["@hw:0001"])


def table_bytes(flavour, label):
    import re
    t = pathlib.Path(HERE, "..", "src", "flavours", flavour, "flavour_data.h").read_text()
    body = re.search(rf"BAS_DATA_TABLE\({label}, \d+\) = \{{(.*?)\}};", t, re.S).group(1)
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", body))


def si5338_expected(flavour, mclk, fine, best, clocks):
    """libs/clock/si5338 with IEEE doubles exactly as BASCOM computes them."""
    import math
    regs = [0] * 256
    data = table_bytes(flavour, "si5338_data")
    n = int.from_bytes(data[:2], "little") // 3
    for i in range(n):
        r, v, m = data[2 + 3 * i:5 + 3 * i]
        if m == 0xFF:
            regs[r] = v
        elif m:
            regs[r] = (regs[r] & ~m & 0xFF) | (v & m)
    fvco = 25000000000.0
    if 1 <= best <= 4:
        da = float(clocks[best - 1])
        if int(math.fmod(fvco, da)) > 0:
            fvco = float(int(fvco / da) + 1) * da
    msn = table_bytes(flavour, "si5338_calc_msn_reg")
    out = {}
    for number, c in [(5, (mclk + fine) & 0xFFFFFFFF)] + [(i + 1, clocks[i]) for i in range(4)]:
        dc = float(c)
        a = int(fvco / dc) & 0xFFFF
        b = int(math.fmod(fvco, dc))
        g = math.gcd(b, c) if b and c else 1
        if b == 0:
            c = 1
        elif g > 1:
            b //= g
            c //= g
        da = float(a) * float(c) + float(b)
        p1 = int((da * 128) / float(c) - 512) & 0x3FFFF
        p2 = int(math.fmod(float(b) * 128, float(c)))
        p3 = c & 0x3FFFFFFF
        reg = msn[number]
        old_top = regs[reg + 9]
        val = p1 | (p2 << 18)
        bs = bytearray(val.to_bytes(6, "little")) + bytearray(p3.to_bytes(4, "little"))
        bs[9] |= old_top
        out[reg] = bytes(bs)
    return out


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class Si5338Test(unittest.TestCase):
    def regs_after_load(self, flavour, eeprom):
        p, _ = run(flavour, ["#i2cdev=1,E0", "#si5338", "#regs=1,E0,256"], eeprom=eeprom, want_eeprom=True)
        line = [l for l in p.stdout.splitlines() if l.startswith("@regs:")][0]
        return bytes.fromhex(line[6:])

    def eeprom_with_clock(self, flavour, mclk, fine, best, clocks):
        L = layout(flavour)
        ee = bytearray(image(flavour))
        a = L["si5338_mclk_eram"][0]
        ee[a:a + 4] = mclk.to_bytes(4, "little")
        a = L["si5338_custom_mclk_fine_tune"][0]
        ee[a:a + 2] = (fine & 0xFFFF).to_bytes(2, "little")
        ee[L["si5338_out_best_jitter_eram"][0]] = best
        a = L["si5338_out_clock_eram"][0]
        for i, c in enumerate(clocks):
            ee[a + 4 * i:a + 4 * i + 4] = c.to_bytes(4, "little")
        return bytes(ee)

    def test_defaults_after_sysinit(self):
        for f in ("V40-96ELN", "V40-CORED8", "V41-96ELN-3"):
            init = table_bytes(f, "si5338_out_clock_init")
            clocks = [int.from_bytes(init[4 * i:4 * i + 4], "little") for i in range(1, 5)]
            mclk, best = consts(f, "SI5338_MCLK_INIT", "SI5338_OUT_BEST_JITTER_INIT")
            regs = self.regs_after_load(f, None)  # blank EEPROM -> sysinit defaults
            for reg, bs in si5338_expected(f, mclk, 0, best, clocks).items():
                self.assertEqual(regs[reg:reg + 10].hex(), bs.hex(), f"{f} reg {reg}")

    def test_custom_clocks_match_double_math(self):
        f = "V41-96ELN-3"
        cases = [
            (491520000, 0, 4, [245760000, 225792000, 122880000, 1000000000]),
            (491520000, -37, 1, [245760000, 225792000, 122880000, 1000000000]),
            (451584000, 12, 2, [112896000, 225792000, 61440000, 270000000]),
            (491520000, 0, 3, [245760001, 225792007, 199999999, 3]),
        ]
        for mclk, fine, best, clocks in cases:
            regs = self.regs_after_load(f, self.eeprom_with_clock(f, mclk, fine, best, clocks))
            for reg, bs in si5338_expected(f, mclk, fine, best, clocks).items():
                self.assertEqual(regs[reg:reg + 10].hex(), bs.hex(), f"{mclk} {clocks} reg {reg}")

    def test_not_fitted(self):
        p, _ = run("V40-CORED8", ["#si5338", "1|get.1 system.errors"], want_eeprom=True)
        self.assertIn("system.errors=[[", p.stdout)  # si5338_rdy + offset_other
        self.assertIn("@hw:0001", p.stdout)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class Tas3108Test(unittest.TestCase):
    """DSP program EEPROMs: compare with the embedded program, rewrite only on mismatch."""

    def program(self, f, n):
        """the program the build carries: the optimized one (FORGE_DSP_OPT, #47) where it exists"""
        label = "dsp_1_code" if n == 1 else "dsp_2_code"
        opt = pathlib.Path(HERE, "..", "src", "flavours", f, "dsp_opt.h")
        if opt.exists():
            import re
            body = re.search(rf"BAS_DATA_TABLE\({label}_opt, \d+\) = \{{(.*?)\}};", opt.read_text(), re.S).group(1)
            code = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", body))
        else:
            code = table_bytes(f, label)
        size = int.from_bytes(code[:2], "little")
        return code[2:2 + size]

    def run_dsp(self, f, ee, dspees, lines, dump=()):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            pre = []
            for n, img in dspees.items():
                pth = pathlib.Path(d, f"dsp{n}.bin")
                pth.write_bytes(img)
                pre.append(f"#dspee={n},{pth}")
            for n in dump:
                pre.append(f"#dspee_out={n},{d}/out{n}.bin")
            p, _ = run(f, pre + lines, eeprom=ee, want_eeprom=True)
            outs = {n: pathlib.Path(d, f"out{n}.bin").read_bytes() for n in dump}
        return p.stdout, outs

    def original_device(self, f):
        cores, = consts(f, "MODUL_TAS3108_CORES")
        ee = bytearray(image(f))
        L = layout(f)
        dsp = {}
        for n in range(1, cores + 1):
            prog = self.program(f, n)
            dsp[n] = prog + b"\xff" * (65536 - len(prog))
            a = L[f"dsp_{n}_eeprom_chksum_eram"][0]
            ee[a:a + 2] = (sum(prog) & 0xFFFF).to_bytes(2, "little")
        return cores, bytes(ee), dsp

    def test_original_device_is_not_rewritten(self):
        for f in FLAVOURS:
            cores, ee, dsp = self.original_device(f)
            out, _ = self.run_dsp(f, ee, dsp, [f"#dsp_eeprom={n}" for n in range(1, cores + 1)])
            for n in range(1, cores + 1):
                self.assertRegex(out, rf"@dsp_eeprom{n}: hw=\w+ writes=0\n", f)

    def test_new_program_is_written_without_factory_reset(self):
        """#47: a firmware with another program writes it although the stored checksum is the old one."""
        f = "V40-96ELN"
        cores, ee, dsp = self.original_device(f)
        prog = self.program(f, 1)
        dsp[1] = bytes(b ^ 0x55 if i == 100 else b for i, b in enumerate(prog)) + b"\xff" * (65536 - len(prog))
        L = layout(f)
        a = L["dsp_1_eeprom_chksum_eram"][0]
        ee = bytearray(ee)
        ee[a:a + 2] = (sum(dsp[1][:len(prog)]) & 0xFFFF).to_bytes(2, "little")   # stored checksum = what is in the DSP EEPROM
        out, res = self.run_dsp(f, bytes(ee), dsp, ["#dsp_eeprom=1"], dump=(1,))
        self.assertNotRegex(out, r"@dsp_eeprom1: hw=\w+ writes=0\n")            # the original would not write
        self.assertEqual(res[1][:len(prog)], prog)

    def test_mismatch_rewrites_program(self):
        f = "V41-96ELN-3"
        cores, ee, dsp = self.original_device(f)
        dsp[3] = b"\x00" * 65536  # damaged program EEPROM of core 3
        out, res = self.run_dsp(f, ee, dsp, ["#dsp_eeprom=3"], dump=(3,))
        prog = self.program(f, 3)
        self.assertIn(f"@dsp_eeprom3: hw=0020 writes={32001 + len(prog)}", out)  # 32001 x 0xFF + program
        self.assertEqual(res[3][:len(prog)], prog)
        self.assertEqual(set(res[3][len(prog):32001]), {0xFF})
        self.assertEqual(set(res[3][32001:]), {0x00})  # beyond dsp_eeprom_clear_count untouched

    def test_sysinit_stores_checksum(self):
        f = "V40-CORED8"
        p, ee = run(f, ["#dsp_eeprom=1"], want_eeprom=True)  # blank ERAM -> sysinit
        a = layout(f)["dsp_1_eeprom_chksum_eram"][0]
        self.assertEqual(int.from_bytes(ee[a:a + 2], "little"), sum(self.program(f, 1)) & 0xFFFF)

    def test_core_detection(self):
        f = "V41-96ELN-3"
        # core 1 answers with state 0, core 3 reports a state error, core 2 missing
        out, _ = self.run_dsp(f, image(f), {}, ["#i2cdev=1,68", "#i2cdev=2,6A,5=1",
                                                "#dsp_core=1", "#dsp_core=2", "#dsp_core=3",
                                                "1|get.1 system.errors"])
        self.assertIn("@dsp_core: hw=0011", out)  # tas3108_1_rdy (0) | tas3108_3_rdy (4)
        # tas3108_2_rdy + errorcount_offset_dsp = 3, errorcount_sw_dsp = 34
        self.assertIn("system.errors=[[3,1],[34,1]]", out)

    def test_core_late(self):
        """#45: a core still loading (NACK) is probed again, up to 8 times."""
        f = "V41-96ELN-3"
        out, _ = self.run_dsp(f, image(f), {}, ["#i2cdev=1,68", "#i2cdev=2,68", "#i2cdev=2,6A",
                                                "#i2cnack=2,68,3", "#i2cnack=2,6A,9",
                                                "#dsp_core=1", "#dsp_core=2", "#dsp_core=3",
                                                "1|get.1 system.errors", "1|get.1 system.hardware.dsp"])
        self.assertIn("@dsp_core: hw=0005", out)       # core 2 after 3 retries, core 3 never (9 > 8)
        self.assertIn("system.errors=[[5,1]]", out)    # only core 3 counted as missing
        self.assertIn("system.hardware.dsp=[0,0,0,0,0,0,0,0,0,11,0]", out)

    def test_errors_clear(self):
        """set system.errors=0 empties the counters (port extension); new errors count from 1 again"""
        f = "V41-96ELN-3"
        out, _ = self.run_dsp(f, image(f), {}, ["#i2cdev=1,68", "#i2cdev=2,68", "#i2cnack=2,6A,9", "#dsp_core=3",
                                                "1|get.1 system.errors", "1|set.1 system.errors=1",
                                                "1|set.1 system.errors=0", "1|get.1 system.errors",
                                                "#dsp_core=3", "1|get.1 system.errors"])
        got = [l for l in out.splitlines() if "system.errors" in l]
        self.assertIn("system.errors=[[5,1]]", got[0])
        self.assertIn("system.errors command out of range", got[1])
        self.assertIn("system.errors ok", got[2])
        self.assertIn("system.errors=[]", got[3])
        self.assertIn("system.errors=[[5,1]]", got[4])

    def test_restart_once(self):
        """#45: cores missing at the end of systeminit -> one software reset, then go on."""
        f = "V41-96ELN-3"
        out, _ = self.run_dsp(f, image(f), {}, ["#i2cdev=1,68", "#dsp_detect", "#dsp_restart", "#dsp_restart",
                                                "#dsp_restart", "1|get.1 system.hardware.dsp"])
        r = [l for l in out.splitlines() if l.startswith("@dsp_restart") or l == "@soft_reset"]
        self.assertEqual(r, ["@soft_reset", "@dsp_restart: hw=0001 flag=1 restarted=0",
                             "@dsp_restart: hw=0001 flag=255 restarted=1",     # second start: no loop
                             "@soft_reset", "@dsp_restart: hw=0001 flag=1 restarted=0"])
        out, _ = self.run_dsp(f, image(f), {}, ["#i2cdev=1,68", "#i2cdev=2,68", "#i2cdev=2,6A", "#dsp_eeprom=1",
                                                "#dsp_eeprom=2", "#dsp_eeprom=3", "#dsp_detect",
                                                "#dsp_restart", "1|get.1 system.hardware.dsp"])
        self.assertIn("@dsp_restart: hw=003F flag=255 restarted=0", out)    # all found: no reset
        self.assertNotIn("@soft_reset", out)
        # a core that answers but reports an error state (program not loaded): restart once as well
        out, _ = self.run_dsp(f, image(f), {}, ["#i2cdev=1,68", "#i2cdev=2,68", "#i2cdev=2,6A,5=255", "#dsp_eeprom=1",
                                                "#dsp_eeprom=2", "#dsp_eeprom=3", "#dsp_detect", "#dsp_restart"])
        self.assertIn("@soft_reset", out)
        self.assertIn("@dsp_restart: hw=003F flag=1 restarted=0", out)

    def test_boot_record(self):
        """#57: get system.hardware.dsp.1/.2 = what the cores reported in this start / before the restart."""
        f = "V41-96ELN-3"
        out, _ = self.run_dsp(f, image(f), {}, ["1|get.1 system.hardware.dsp.2", "#i2cdev=1,68", "#i2cdev=2,68",
                                                "#i2cdev=2,6A,5=255", "#dsp_eeprom=1", "#dsp_eeprom=2",
                                                "#dsp_eeprom=3", "#dsp_detect", "1|get.1 system.hardware.dsp.1",
                                                "#dsp_restart", "1|get.1 system.hardware.dsp.2",
                                                "1|get.1 system.hardware.dsp.3"])
        self.assertIn("system.hardware.dsp.2=[0]", out)   # nothing kept yet
        # empty EEPROMs: every core rewritten once and read back correctly; core 3 state byte 1 = 0x55
        core3 = "[1,1,1,0,0,0,85,0,0,0,0,0]"
        rec = f"[1,[1,1,1,0,0,0,0,0,0,0,0,0],[1,1,1,0,0,0,0,0,0,0,0,0],{core3},[0,0,0,0,0],[0,0,0,0,0]]"
        self.assertIn("system.hardware.dsp.1=" + rec, out)
        self.assertIn("system.hardware.dsp.2=" + rec, out)
        self.assertIn("system.hardware.dsp.3 command out of range", out)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class DacTest(unittest.TestCase):
    def transfers(self, f, lines):
        p = subprocess.run([os.path.join(NATIVE, f"cmd-{f}")], input="\n".join(lines) + "\n",
                           capture_output=True, text=True, timeout=10)
        self.assertEqual(p.returncode, 0, p.stderr)
        return [l for l in p.stdout.splitlines() if l.startswith("@")]

    def test_es90x8pro(self):
        out = self.transfers("V41-96ELN-3", ["#i2cdev=2,90", "#i2clog=1", "#dac_init"])
        mute = [f"@i2c2: S 90 {r:02X} FF P" for r in (0x16, 0x17, 0x14, 0x15, 0x12, 0x13, 0x10, 0x11)]
        self.assertEqual(out, ["@i2c2: S 90 P", "@i2c2: S 90 07 Sr 91 r00N P",
                               "@i2c2: S 90 07 40 P", "@i2c2: S 90 26 31 P", "@i2c2: S 90 27 20 P",
                               "@i2c2: S 90 28 57 P", "@i2c2: S 90 29 46 P", "@i2c2: S 90 00 F0 P",
                               "@i2c2: S 90 01 00 P", "@i2c2: S 90 02 3D P",
                               "@i2c2: S 90 40 Sr 91 r00N P"] + mute + ["@hw:0005"])

    def test_es9018(self):
        for f in ("V40-CORED8", "V40-96ELN", "V30-96ELN"):
            out = self.transfers(f, ["#i2cdev=2,90", "#i2clog=1", "#dac_init"])
            mute = [f"@i2c2: S 90 {r:02X} FF P" for r in (6, 7, 4, 5, 2, 3, 0, 1)]
            self.assertEqual(out, ["@i2c2: S 90 P", "@i2c2: S 90 08 7F P", "@i2c2: S 90 0A DE P",
                                   "@i2c2: S 90 0E 0A P", "@i2c2: S 90 11 1C P",
                                   "@i2c2: S 90 1B Sr 91 r00N P"] + mute + ["@hw:0005"], f)

    def test_dac_missing(self):
        out = self.transfers("V41-96ELN-3", ["#dac_init", "1|get.1 system.errors"])
        self.assertIn("@hw:0001", out)
        self.assertIn("@0: <.1 system.errors=[[19,1]]\\r\\n", out)  # es90x8pro_rdy 2 + 17


def ct7302_init_transfers(adr, opt32=False):
    """libs/spdif/ct7302/functions.bas ct7302_init_sub on a device with all registers 0x00"""
    a, ar = f"{adr:02X}", f"{adr + 1:02X}"
    rmw = lambda reg, v: [f"@i2c1: S {a} {reg:02X} Sr {ar} r00N P", f"@i2c1: S {a} {reg:02X} {v:02X} P"]
    w = lambda reg, v: [f"@i2c1: S {a} {reg:02X} {v:02X} P"]
    t = (rmw(0x05, 0x13) + w(0x07, 0x02) + rmw(0x11, 0) + rmw(0x13, 0) + rmw(0x14, 0) +
         w(0x30, 0x23) + w(0x31, 0x19) + w(0x32, 0x1E) + rmw(0x40, 0) + rmw(0x45, 0) + rmw(0x4D, 0x02) +
         w(0x4E, 0x33) + w(0x61, 0x08) + w(0x62, 0x01) + w(0x4C, 0x10) + w(0x04, 0x01) + w(0x06, 0x78) +
         w(0x60, 0x17))
    if opt32:
        t += rmw(0x77, 0x40)
    return t


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class Ct7302Test(unittest.TestCase):
    def transfers(self, f, lines, eeprom=None):
        p, _ = run(f, lines, eeprom=eeprom, want_eeprom=True)
        return [l for l in p.stdout.splitlines() if l.startswith("@")]

    def test_init_both_receivers(self):
        for f in ("V41-96ELN-3", "V40-CORED8", "V30-96ELN"):
            out = self.transfers(f, ["#i2cdev=1,20", "#i2cdev=1,22", "#i2clog=1", "#spdif_init"], eeprom=image(f))
            # ct7302_register_options is only written by MODC "spdif", never at sysinit:
            # an erased EEPROM (0xFF) has bit 7 set -> SPDIF RX 32 bit
            exp = (["@i2c1: S 20 P"] + ct7302_init_transfers(0x20, True) + ["@i2c1: S 22 P"] +
                   ct7302_init_transfers(0x22, True) + ["@hw:0019"])
            self.assertEqual(out, exp, f)

    def test_options_without_32bit(self):
        f = "V41-96ELN-3"
        ee = bytearray(image(f))
        ee[layout(f)["ct7302_register_options"][0]] = 0x01  # bit 0 has no code in the original
        out = self.transfers(f, ["#i2cdev=1,20", "#i2clog=1", "#spdif_init"], eeprom=bytes(ee))
        self.assertEqual(out[:1 + len(ct7302_init_transfers(0x20))],
                         ["@i2c1: S 20 P"] + ct7302_init_transfers(0x20))
        self.assertEqual(out[1 + len(ct7302_init_transfers(0x20))], "@i2c1: S 22 P")

    def test_usb_input_is_rx5(self):
        out = self.transfers("V41-96ELN-3", ["#i2cdev=1,20", "#spdif_init", "#ct7302_mux=1", "#ct7302_mux=3",
                                             "#regs=1,20,8"])
        self.assertIn("@mux:1", out)  # stored as 5 (RX5 = I2S from the XMOS), reported as 1
        self.assertIn("@mux:3", out)
        self.assertEqual([l for l in out if l.startswith("@regs")], ["@regs:0000000003137802"])


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class SysteminitTest(unittest.TestCase):
    """Generated peripheral sequence of systeminit.bas on a fully populated bus model."""
    ALL = ["#i2cdev=1,E0", "#i2cdev=1,40", "#i2cdev=1,4E", "#i2cdev=2,90", "#i2cdev=1,20",
           "#i2cdev=1,22", "#i2cdev=1,68", "#i2cdev=2,68", "#i2cdev=2,6A",
           "#i2cdev=2,30", "#i2cdev=1,98", "#i2cdev=2,20"]  # + V30-96LN: CS4385, CS5366, CS8416

    def complete(self, f):
        return "COMPLETE 1" in pathlib.Path(HERE, "..", "src", "flavours", f, "flavour_systeminit.h").read_text()

    def test_detected_hardware_matches_original_check(self):
        for f in FLAVOURS:
            if not self.complete(f):
                continue
            p, _ = run(f, self.ALL + ["#systeminit"], eeprom=image(f), want_eeprom=True)
            line = [l for l in p.stdout.splitlines() if l.startswith("@hw:")][0]
            other, dsp = consts(f, "CHK_HARDWARE_OTHER", "CHK_HARDWARE_DSP")
            if f == "V30-96ELN":
                # original: chk_hardware_other expects the Si5338 (bit 11), which this build does
                # not have, and not the DAC/SPDIF bits (2, 3) it detects -> always muted (#32)
                self.assertEqual(other, 0x0871)
                self.assertEqual(line, f"@hw:007D dsp:{dsp:04X}", f)
            else:
                self.assertEqual(line, f"@hw:{other:04X} dsp:{dsp:04X}", f)
            # checks evaluate: fully populated -> no hardware mute (port fix for V30-96ELN)
            p, _ = run(f, self.ALL + ["#systeminit", "#hw_check"], eeprom=image(f), want_eeprom=True)
            self.assertIn("@hwcheck:0 mute:0", p.stdout, f)

    def test_missing_device_mutes(self):
        """systeminit.bas "checks evaluate": a missing peripheral keeps the outputs muted."""
        f = "V40-96ELN"
        missing = [x for x in self.ALL if x != "#i2cdev=2,6A"]   # third DSP core
        p, _ = run(f, missing + ["#systeminit", "#hw_check", "1|get.1 system.feature.attributes.hw"],
                   eeprom=image(f), want_eeprom=True)
        self.assertIn("@hwcheck:1 mute:255", p.stdout)  # api_mute_out_on
        # #25: a V40 build on a V30 board (no Si5338) is caught and stays muted
        no_clock = [x for x in self.ALL if x != "#i2cdev=1,E0"]
        p, _ = run(f, no_clock + ["#systeminit", "#hw_check"], eeprom=image(f), want_eeprom=True)
        self.assertIn("@hwcheck:1 mute:255", p.stdout)
        f = "V30-96ELN"
        p, _ = run(f, ["1|get.1 system.feature.attributes.hw"], eeprom=image(f), want_eeprom=True)
        self.assertIn("=[", p.stdout)
        self.assertEqual(p.stdout.split("=[")[1].split(",")[4], "125")   # expected other = 0x007D

    def test_reset_order_96eln3(self):
        f = "V41-96ELN-3"
        p, _ = run(f, self.ALL + ["#i2clog=1", "#systeminit"], eeprom=image(f), want_eeprom=True)
        ev = [l for l in p.stdout.splitlines() if l.startswith(("@pin", "@sei")) or
              l in ("@i2c1: S 4E 02 7F P", "@i2c1: S 4E 02 FF P") or l.startswith("@i2c2: S 90 16 FF")]
        # bit 6 on, DAC mute, interrupts, reset 1, bit 7 on, reset 2
        self.assertEqual(ev, ["@i2c1: S 4E 02 7F P", "@i2c2: S 90 16 FF P", "@sei",
                              "@pin:PIN_PERIPHERAL_DEVICES_RESET1=1", "@i2c1: S 4E 02 FF P",
                              "@pin:PIN_PERIPHERAL_DEVICES_RESET2=1"])

    def test_all_flavours_complete(self):
        self.assertTrue(all(self.complete(f) for f in FLAVOURS))

    def test_96ln_order(self):
        f = "V30-96LN"
        p, _ = run(f, self.ALL + ["#i2clog=1", "#systeminit"], eeprom=image(f), want_eeprom=True)
        ev = [l for l in p.stdout.splitlines() if l.startswith(("@pin", "@sei")) or
              l in ("@i2c2: S 30 02 9E P", "@i2c2: S 30 02 DE P", "@i2c2: S 30 02 80 P",
                    "@i2c1: S 98 01 93 P", "@i2c1: S E0 01 3F P", "@i2c2: S 20 01 84 P")]
        # spdif (SRC43x2, CS8416) before reset 2, CS4385 right after reset 2, "DA ready" and the
        # ADC only after the cores
        self.assertEqual(ev, ["@sei", "@pin:PIN_PERIPHERAL_DEVICES_RESET1=1", "@i2c1: S E0 01 3F P",
                              "@i2c2: S 20 01 84 P", "@pin:PIN_PERIPHERAL_DEVICES_RESET2=1",
                              "@i2c2: S 30 02 9E P", "@i2c2: S 30 02 DE P", "@i2c2: S 30 02 80 P",
                              "@i2c1: S 98 01 93 P"])


def bq_reference(flavour, t, f, g, q, b, flags):
    """libs/dsp/tas3108 biquad formulas in IEEE double, independent of the C port."""
    import math
    import struct
    pw = table_bytes(flavour, "data_power_gain")
    table = [struct.unpack("<d", pw[i:i + 8])[0] for i in range(0, len(pw), 8)]
    if not flags & 0x80:
        t = 0
    F, Qv, G, B = float(f), q / 1000.0, g / 10.0, float(b)
    fs = 96000

    def omega(fr):
        return (6.283185307179586476925286766559 * fr) / fs

    def alpha(w, qq):
        return math.sin(w) / (2.0 * qq)

    def power(gain):
        g0 = gain
        gain = gain * 10
        l = int(gain)
        if abs(gain - l) > 0.001 and -120 <= l <= 120:   # PORT (#51): between table steps
            return 10.0 ** (g0 / 40.0)
        neg = l < 0
        l = abs(l)
        c = l & 0xFF
        if c > 120:
            c = 0
        p = table[c]
        return 1.0 / p if neg else p

    c = {}
    def high(qq):
        w = omega(F); a = alpha(w, qq); co = math.cos(w)
        c.update(B0=(1.0 + co) / 2.0, B1=-(1.0 + co), A0=1.0 + a, A1=-(-2.0 * co), A2=-(1.0 - a))
        c["B2"] = c["B0"]

    def low(qq):
        w = omega(F); a = alpha(w, qq); co = math.cos(w)
        c.update(B0=(1.0 - co) / 2.0, B1=1.0 - co, A0=1.0 + a, A1=-(-2.0 * co), A2=-(1.0 - a))
        c["B2"] = c["B0"]

    def shelf(high_):
        w = omega(F); P = power(G); co = math.cos(w)
        b1, b2 = P + 1.0, P - 1.0
        b3, b4 = b1 * co, b2 * co
        bso = math.sqrt(P) / Qv * math.sin(w)
        if high_:
            c.update(B0=P * ((b1 + b4) + bso), B1=-2.0 * (P * (b2 + b3)), B2=P * ((b1 + b4) - bso),
                     A0=(b1 - b4) + bso, A1=-2.0 * (b2 - b3), A2=-((b1 - b4) - bso))
        else:
            c.update(B0=P * ((b1 - b4) + bso), B1=2.0 * (P * (b2 - b3)), B2=P * ((b1 - b4) - bso),
                     A0=(b1 + b4) + bso, A1=2.0 * (b2 + b3), A2=-((b1 + b4) - bso))

    def eq(qq):
        w = omega(F); a = alpha(w, qq); co = math.cos(w); P = power(G)
        c.update(A0=1.0 + a / P, A1=-(-2.0 * co), A2=-(1.0 - a / P), B0=1.0 + a * P, B1=-2.0 * co,
                 B2=1.0 - a * P)

    qb, qe, ql, qc = 0.7071067811865476, 0.5773502691896257, 0.5, 1.0
    if t == 1: eq(Qv)
    elif t == 8:
        w = omega(F); a = alpha(w, 1.0)
        c.update(A0=1.0 + a, A1=1.0 - a, A2=0.0, B0=1.0, B1=-1.0, B2=0.0)
    elif t == 9: high(qb)
    elif t == 10: high(qe)
    elif t == 11: high(ql)
    elif t == 13: high(Qv)
    elif t == 12: high(qc)
    elif t == 2:
        w = omega(F); a = alpha(w, 1.0)
        c.update(A0=1.0 + a, A1=1.0 - a, A2=0.0, B0=1.0 * a, B1=1.0 * a, B2=0.0)
    elif t == 3: low(qb)
    elif t == 4: low(qe)
    elif t == 5: low(ql)
    elif t == 7: low(Qv)
    elif t == 6: low(qc)
    elif t == 14: shelf(False)
    elif t == 15: shelf(True)
    elif t == 16:
        w = omega(F); a = alpha(w, F / B); co = math.cos(w)
        c.update(A0=1.0 + a, A1=2.0 * co, A2=-(1.0 - a), B0=1.0, B1=-2.0 * co, B2=1.0)
    elif t == 17:  # BandPass as CalculatorBandPass of the GUI (port extension, #20)
        w = omega(F); a = alpha(w, Qv); co = math.cos(w); sn = math.sin(w)
        c.update(B0=sn / 2.0, B1=0.0, B2=-(sn / 2.0), A0=1.0 + a, A1=2.0 * co, A2=-(1.0 - a))
    elif t == 18: eq(F / B)
    elif t == 19:
        c.update(A0=1.0, A1=1.866634, A2=-0.8670602, B0=0.1170960, B1=-0.0999452, B2=-0.0129345)
    elif t == 20:
        w = omega(F); a = alpha(w, Qv); co = math.cos(w)
        c.update(A0=1.0 + a, B2=1.0 + a, A1=2.0 * co, B1=-(2.0 * co), B0=1.0 - a, A2=-(1.0 - a))
    else:
        c.update(A0=1.0, A1=0.0, A2=0.0, B0=1.0, B1=0.0, B2=0.0)
    a0 = c["A0"]
    order = [c["A2"] / a0, c["A1"] / a0, c["B2"] / a0, c["B1"] / a0, c["B0"] / a0]
    return [(((int(v * 8388608.0) + 134217728) & 0xFFFFFFFF) ^ 0x08000000) for v in order]


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class BiquadTest(unittest.TestCase):
    def test_all_types_against_reference(self):
        import itertools
        import random
        f = "V41-96ELN-3"
        rnd = random.Random(1)
        cases = []
        for t in range(0, 21):  # incl. 17 BandPass (port extension)
            for fr, g, q in itertools.product((10, 20, 63, 1000, 8000, 22000), (-120, -35, 0, 35, 60),
                                              (1, 500, 707, 2000, 10000)):
                if rnd.random() < 0.25:
                    cases.append((t, fr, g, q, rnd.choice((0, 10, 100, 1000)) or 1, 0x80))
        cases += [(1, 1000, 30, 1000, 0, 0x00), (14, 80, 125, 707, 0, 0x80), (15, 10000, -130, 707, 0, 0x80)]
        p = subprocess.run([os.path.join(NATIVE, f"cmd-{f}")],
                           input="\n".join(f"#bq_calc={','.join(map(str, c))}" for c in cases) + "\n",
                           capture_output=True, text=True, timeout=30)
        got = [l[4:] for l in p.stdout.splitlines() if l.startswith("@bq:")]
        self.assertEqual(len(got), len(cases))
        for c, g in zip(cases, got):
            exp = " ".join(f"{v:08X}" for v in bq_reference(f, *c))
            self.assertEqual(g, exp, c)

    def test_known_values(self):
        # all pass / bypass: b0 = 1.0 = 0x00800000, rest 0
        out = run("V41-96ELN-3", ["#bq_calc=0,1000,0,707,0,128"])
        self.assertEqual(out[0], ["@bq:00000000 00000000 00000000 00000000 00800000"])
        # negative coefficient as 28-bit two's complement (a2 of a lowpass)
        out = run("V41-96ELN-3", ["#bq_calc=3,1000,0,707,0,128"])
        self.assertTrue(out[0][0].startswith("@bq:0F"), out)


def bq_response(raw_hex, freq, fs=96000):
    """complex response of "@bq:A2 A1 B2 B1 B0" (5.23, a1/a2 stored negated as the DSP adds them)"""
    import cmath
    v = []
    for h in raw_hex.split():
        x = int(h, 16) & 0x0FFFFFFF
        if x & 0x08000000:
            x -= 1 << 28
        v.append(x / 8388608.0)
    a2, a1, b2, b1, b0 = v
    z1 = cmath.exp(-2j * math.pi * freq / fs)
    return (b0 + b1 * z1 + b2 * z1 * z1) / (1 - a1 * z1 - a2 * z1 * z1)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class AllpassTest(unittest.TestCase):
    """#48: all pass types 20 (2nd order, RBJ), 21 (1st order) and 22 (wanted phase lag at f),
    checked against the frequency response, not against a copy of the formulas."""

    def calc(self, *cases):
        p = subprocess.run([os.path.join(NATIVE, "cmd-V41-96ELN-3")],
                           input="\n".join(f"#bq_calc={','.join(map(str, c))}" for c in cases) + "\n",
                           capture_output=True, text=True, timeout=30)
        return [l[4:] for l in p.stdout.splitlines() if l.startswith("@bq:")]

    def phase(self, raw, f):
        import cmath
        return math.degrees(cmath.phase(bq_response(raw, f)))

    def flat(self, raw):
        for f in (20, 100, 1000, 5000, 20000):
            self.assertAlmostEqual(abs(bq_response(raw, f)), 1.0, delta=2e-4, msg=(raw, f))

    def test_type20_rbj(self):
        for f, q in ((80, 707), (1000, 2000), (8000, 500)):
            raw, = self.calc((20, f, 0, q, 0, 128))
            self.flat(raw)
            self.assertAlmostEqual(abs(self.phase(raw, f)), 180.0, delta=0.5, msg=f)   # -180 at f0
            self.assertLess(abs(self.phase(raw, f / 100)), 5.0)    # 0 deg far below f0

    def test_type21_first_order(self):
        for f in (40, 80, 1000, 10000):
            raw, = self.calc((21, f, 0, 707, 0, 128))
            self.flat(raw)
            self.assertAlmostEqual(self.phase(raw, f), -90.0, delta=0.2, msg=f)
            self.assertGreater(abs(self.phase(raw, 47900)), 170.0)   # towards -180 at fs/2
            self.assertLess(abs(self.phase(raw, f / 100)), 2.0)

    def test_type22_wanted_angle(self):
        for f, deg, q in ((80, 45, 0), (80, 90, 0), (200, 170, 0), (80, 180, 707), (80, 270, 707),
                          (1000, 300, 2000), (60, 359, 500), (5000, 1, 0)):
            raw, = self.calc((22, f, 0, q, deg, 128))
            self.flat(raw)
            got = -self.phase(raw, f) % 360
            self.assertAlmostEqual(got, deg, delta=0.5, msg=(f, deg, q, got))   # 5.23 rounding at low f

    def test_type22_limits(self):
        bypass = "00000000 00000000 00000000 00000000 00800000"
        self.assertEqual(self.calc((22, 80, 0, 707, 0, 128), (22, 80, 0, 707, 360, 128)), [bypass, bypass])
        raw21, raw22 = self.calc((21, 80, 0, 707, 0, 128), (22, 80, 0, 0, 90, 128))
        self.assertEqual(raw21, raw22)            # 90 deg at f = first order with f0 = f


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class DspWriteTest(unittest.TestCase):
    def test_biquad_write_path(self):
        f = "V41-96ELN-3"
        core_t = table_bytes(f, "data_flow_dsp_bq_core")
        reg_t = table_bytes(f, "data_flow_dsp_bq_reg")
        for nr in (1, 37, 200, 536):
            core, reg = core_t[nr], reg_t[nr]
            bus, adr = {1: (1, 0x68), 2: (2, 0x68), 3: (2, 0x6A)}[core]
            p, _ = run(f, ["#i2cdev=1,68", "#i2cdev=2,68", "#i2cdev=2,6A", "#dsp_detect", "#i2clog=1",
                           f"#dsp_bq={nr},1,1000,35,1414,0,128"], eeprom=image(f), want_eeprom=True)
            lines = [l for l in p.stdout.splitlines() if l.startswith(("@i2c", "@dsp"))]
            raw = bq_reference(f, 1, 1000, 35, 1414, 0, 0x80)  # A2, A1, B2, B1, B0
            data = b"".join(v.to_bytes(4, "big") for v in reversed(raw))  # B0 first, MSB first
            # #67: the place is not known yet: read first (empty here, so it is written)
            self.assertTrue(lines[0].startswith(f"@i2c{bus}: S {adr:02X} {reg:02X} Sr {adr + 1:02X}"), nr)
            self.assertEqual(lines[1], f"@i2c{bus}: S {adr:02X} {reg:02X} " + " ".join(f"{x:02X}" for x in data) + " P", nr)
            self.assertTrue(lines[2].startswith(f"@i2c{bus}: S {adr:02X} {reg:02X} Sr {adr + 1:02X}"), nr)
            self.assertEqual(lines[-1], f"@dsp: core={core} reg={reg:02X} verify_errors=0", nr)

    def test_verify_error_is_counted_and_limited(self):
        f = "V41-96ELN-3"
        nr = next(i for i, c in enumerate(table_bytes(f, "data_flow_dsp_bq_core")) if c == 1 and i > 0)
        # core 1 ignores writes: every read-back differs, the write is retried (max. 8 in the port)
        p, _ = run(f, ["#i2cdev=1,68", "#i2cro=1,68", "#dsp_detect", f"#dsp_bq={nr},1,1000,35,1414,0,128",
                       "1|get.1 system.errors"], eeprom=image(f), want_eeprom=True)
        self.assertIn("verify_errors=8", p.stdout)
        self.assertIn(",[129,8]]", p.stdout)  # errorcount_fw_iface_verify (3, 5: cores 2, 3 missing)


class LinesMixin:
    def lines(self, flavour, lines):
        p = subprocess.run([os.path.join(NATIVE, f"cmd-{flavour}")], input="\n".join(lines) + "\n",
                           capture_output=True, text=True, timeout=10)
        self.assertEqual(p.returncode, 0, p.stderr)
        return [l for l in p.stdout.splitlines() if not l.startswith(("@pin", "--"))]


@unittest.skipUnless(available("V40-96ELN"), "native harness not built")
class UsbBaudTest(LinesMixin, unittest.TestCase):
    """#62: set system.baud.usb=N switches the USB-COM after the answer (N = rate / 1200)."""

    def test_switch_and_limits(self):
        out = "\n".join(self.lines("V40-96ELN", ["1|set.1 system.baud.usb=96", "1|set.1 system.baud.usb=384",
                                                 "1|set.1 system.baud.usb=50", "3|set.1 system.baud.usb=96"]))
        # the answer first (at the old rate), then the switch
        self.assertRegex(out, r"system\.baud\.usb ok\\r\\n\s*@usb_baud:115200")
        self.assertIn("@usb_baud:460800", out)
        self.assertIn("system.baud.usb command out of range", out)
        self.assertIn("system.baud.usb unknown para", out)              # only from the USB-COM
        self.assertEqual(out.count("@usb_baud"), 2)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class AudioCmdTest(LinesMixin, unittest.TestCase):
    """cmd_gain, cmd_mute, cmd_phase (libs/cmd/v20/common/subs)."""

    def test_gain_roundtrip_and_ranges(self):
        for f in FLAVOURS:
            nin, nout, nsp = consts(f, "FLOW_MAX_IN_CHANNEL", "FLOW_MAX_OUT_CHANNEL", "FLOW_MAX_SPDIF_CHANNEL")
            out = self.lines(f, ["1|set.1 gain.out.3=120", "1|set.1 gain.in.2=7", "1|get.1 gain.out",
                                 "1|get.1 gain.in.2", f"1|set.1 gain.in.{nin + 1}=5", "1|set.1 gain.out.0=5",
                                 f"1|get.1 gain.out.{nout + 1}", "1|get.1 gain.bad"])
            self.assertEqual(out[0], "@0: *.1 gain.out.3 ok\\r\\n", f)
            self.assertEqual(out[2], "@0: <.1 gain.out=[" + ",".join("120" if i == 3 else "0"
                                                                    for i in range(1, nout + 1)) + "]\\r\\n", f)
            self.assertEqual(out[3], "@0: <.1 gain.in.2=7\\r\\n", f)
            self.assertIn("command out of range", out[4], f)
            self.assertIn("command out of range", out[5], f)
            self.assertIn("command out of range", out[6], f)
            self.assertIn("unknown command", out[7], f)

    def test_gain_scales(self):
        import re
        for f in FLAVOURS:
            t = pathlib.Path(HERE, "..", "src", "flavours", f, "flavour.h").read_text()
            scale = re.search(r'#define BAS_DAC_SCALE "(.*)"', t).group(1)
            out = self.lines(f, ["1|get.1 gain.out.scale", "1|get.1 gain.in.scale"])
            self.assertEqual(out[0], f"@0: <.1 gain.out.scale={scale}\\r\\n", f)
            self.assertEqual(out[1], "@0: <.1 gain.in.scale=[0,255,223,0.5,-111.5,8]\\r\\n", f)

    def test_gain_out_writes_dsp(self):
        f = "V41-96ELN-3"
        ovd = table_bytes(f, "data_flow_dsp_output_vol_dsp")
        ovr = table_bytes(f, "data_flow_dsp_output_vol_reg")
        vol = table_bytes(f, "data_dsp_vol")
        adr = {1: (1, 0x68), 2: (2, 0x68), 3: (2, 0x6A)}
        bus, a = adr[ovd[5]]
        out = self.lines(f, ["#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#dsp_detect",
                             "1|set.1 gain.out.5=77", f"#block={bus},{a:02X},{ovr[5]:02X},4"])
        self.assertIn(f"@block:{vol[77 * 4:77 * 4 + 4][::-1].hex().upper()}", out)

    def test_mute_bits_event_and_dac(self):
        f = "V41-96ELN-3"
        volreg = table_bytes(f, "data_es90x8pro_vol_reg")
        out = self.lines(f, ["#i2cdev=2,90", "#dsp_detect", "#dac_init", "#dtr=1", "#ir=0",
                             "1|set.1 mute.out.2=1", "1|set.1 mute.out.5=1", "1|set.1 mute.out.2=0",
                             "1|get.1 mute.out", "1|get.1 mute.out.5", "#regs=2,90,24"])
        ev = "#.1 mute.out=[0,0,0,0,1,0,0,0]\\r\\n"
        self.assertIn("@1: " + ev, out)   # COM master (IR master offline)
        self.assertIn("@0: " + ev, out)   # COM USB (GUI, DTR)
        self.assertIn("@0: <.1 mute.out=[0,0,0,0,1,0,0,0]\\r\\n", out)
        self.assertIn("@0: <.1 mute.out.5=1\\r\\n", out)
        regs = bytes.fromhex([l for l in out if l.startswith("@regs:")][0][6:])
        self.assertEqual([regs[volreg[ch]] for ch in range(1, 9)], [0, 0, 0, 0, 0xFF, 0, 0, 0])

    def test_phase_dsp_and_dac(self):
        f = "V41-96ELN-3"   # phase in the DSP
        ivd = table_bytes(f, "data_flow_dsp_output_inv_dsp")
        ivr = table_bytes(f, "data_flow_dsp_output_inv_reg")
        adr = {1: (1, 0x68), 2: (2, 0x68), 3: (2, 0x6A)}
        on, off = consts(f, "FLOW_PHASE_ON_VALUE", "FLOW_PHASE_OFF_VALUE")
        lines = ["#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#dsp_detect", "1|set.1 phase.out.8=1",
                 "1|get.1 phase.out"]
        for ch in (7, 8):
            bus, a = adr[ivd[ch]]
            lines.append(f"#block={bus},{a:02X},{ivr[ch]:02X},4")
        out = self.lines(f, lines)
        self.assertIn("@0: <.1 phase.out=[0,0,0,0,0,0,0,1]\\r\\n", out)
        blocks = [l[7:] for l in out if l.startswith("@block:")]
        self.assertEqual(blocks, [f"{off:08X}", f"{on:08X}"])
        f = "V40-CORED8"    # phase in the ES9018 (bit pairs reversed)
        out = self.lines(f, ["#i2cdev=2,90", "#dsp_detect", "#dac_init", "1|set.1 phase.out.1=1",
                             "1|set.1 phase.out.8=1", "#regs=2,90,24"])
        reg, = consts(f, "ES9018_REG_DACA_PHASE")
        regs = bytes.fromhex([l for l in out if l.startswith("@regs:")][0][6:])
        self.assertEqual(regs[reg], 0x42)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class DelayCmdTest(LinesMixin, unittest.TestCase):
    """cmd_delay: samples, units, register pairs in the DSP, input delays."""

    def test_delay_values_units_left(self):
        for f in FLAVOURS:
            nout, ndsp, samples = consts(f, "FLOW_MAX_OUT_CHANNEL", "FLOW_MAX_DSP", "FLOW_MAX_SAMPLES")
            out = self.lines(f, ["1|set.1 delay.out.3=100", "1|set.1 delay.2=50", "1|get.1 delay.out",
                                 "1|set.1 delay.out.4.ms=2.5", "1|get.1 delay.4", "1|get.1 delay.out.3.ms",
                                 "1|set.1 delay.out.4.km=2.5", "1|get.1 delay.out.left",
                                 f"1|set.1 delay.out.{nout + 1}=1"])
            self.assertEqual(out[2], "@0: <.1 delay.out=[0,50,100" + ",0" * (nout - 3) + "]\\r\\n", f)
            self.assertEqual(out[4], "@0: <.1 delay.4=240\\r\\n", f)       # 2.5 ms / (1/96 ms)
            self.assertEqual(out[5], "@0: <.1 delay.out.3=1.042\\r\\n", f)  # 100 / 96 kHz
            self.assertIn("unknown command", out[6], f)
            self.assertEqual(len([l for l in out if " ok" in l]), 3, f)       # no ok after the error
            # PORT (#53): free samples per DSP after the real mapping (data_flow_pair_delay_dsp)
            dd = table_bytes(f, "data_flow_pair_delay_dsp")
            vals = [0, 50, 100, 240] + [0] * (nout - 4)
            left = [samples - sum(v for i, v in enumerate(vals, 1) if dd[i] == dsp) for dsp in range(1, ndsp + 1)]
            self.assertEqual(out[7], "@0: <.1 delay.out=[" + ",".join(map(str, left)) + "]\\r\\n", f)
            self.assertIn("command out of range", out[8], f)

    def test_delay_pairs_in_dsp(self):
        f = "V41-96ELN-3"
        dd = table_bytes(f, "data_flow_pair_delay_dsp")
        dr = table_bytes(f, "data_flow_pair_delay_reg")
        adr = {1: (1, 0x68), 2: (2, 0x68), 3: (2, 0x6A)}
        bus, a = adr[dd[3]]
        out = self.lines(f, ["#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#dsp_detect",
                             "1|set.1 delay.out.3=100", "1|set.1 delay.out.4=7",
                             f"#block={bus},{a:02X},{dr[3]:02X},4"])
        # mode 1 stores samples * 2; odd channel = high word
        self.assertIn(f"@block:{(200 << 16 | 14):08X}", out)

    def test_delay_memory_per_dsp(self):
        """#53: the delay lines of one DSP share flow_max_samples; a value beyond is refused."""
        for f in ("V40-96ELN", "V30-96LN"):
            samples, = consts(f, "FLOW_MAX_SAMPLES")
            dd = table_bytes(f, "data_flow_pair_delay_dsp")
            a = [i for i in range(1, 9) if dd[i] == dd[1]]           # outputs on the DSP of output 1
            other = next(i for i in range(1, 9) if dd[i] != dd[1])
            out = self.lines(f, ["1|set.1 delay=[0,0,0,0,0,0,0,0]",
                                 f"1|set.1 delay.{a[0]}={samples - 100}", f"1|set.1 delay.{a[1]}=100",
                                 f"1|set.1 delay.{a[2]}=1",                                     # one too many
                                 f"1|set.1 delay.{other}={samples}",                           # another DSP: own memory
                                 f"1|set.1 delay.{a[0]}={samples - 150}", f"1|set.1 delay.{a[2]}=50",
                                 "1|get.1 delay", "1|get.1 delay.left",
                                 f"1|set.1 delay=[{samples},{samples},0,0,0,0,0,0]"])          # array beyond: nothing written
            oks = [("ok" in l) for l in out[:7]]
            self.assertEqual(oks, [True, True, True, False, True, True, True], (f, out))
            vals = [int(x) for x in out[7].split("[")[1].split("]")[0].split(",")]
            self.assertEqual(sum(v for i, v in enumerate(vals, 1) if dd[i] == dd[1]), samples, f)
            left = [int(x) for x in out[8].split("[")[1].split("]")[0].split(",")]
            self.assertEqual(left[dd[1] - 1], 0, f)
            self.assertEqual(left[dd[other] - 1], 0, f)
            if dd[1] == dd[2]:
                self.assertIn("command out of range", out[9], f)

    def test_blank_storage_takes_no_memory(self):
        """#53: storage never written (32767) must not block setting a delay."""
        f = "V40-96ELN"
        out = self.lines(f, BiquadCmdTest.PRE + ["1|get.1 delay.in", "1|set.1 delay.in.1=100", "1|get.1 delay.in.1", "1|get.1 delay.left"])
        out = [l for l in out if "delay" in l]
        self.assertIn("32767", out[0])                                    # the harness storage is blank
        self.assertIn("ok", out[1])
        self.assertIn("delay.in.1=100", out[2])
        dsp1 = int(out[3].split("[")[1].split(",")[0])
        self.assertEqual(dsp1, consts(f, "FLOW_MAX_SAMPLES")[0] - 100)   # DSP 1: only the input delays

    def test_delay_in(self):
        f = "V41-96ELN-3"
        out = self.lines(f, ["1|set.1 delay.out=[11,12,13,14,15,16,17,18]",
                             "1|set.1 delay.in=[1,2,3,4,5,6,7,8]", "1|get.1 delay.in", "1|get.1 delay.out",
                             "1|set.1 delay.in.7=3"])
        self.assertEqual(out[2], "@0: <.1 delay.in=[1,2,3,4,5,6,11,12]\\r\\n")  # 7/8: outputs (original)
        self.assertEqual(out[3], "@0: <.1 delay.out=[11,12,13,14,15,16,17,18]\\r\\n")
        self.assertIn("command out of range", out[4])
        out = self.lines("V30-96LN", ["1|get.1 delay.in", "1|set.1 delay.in.1=3"])
        self.assertIn("unknown command", out[0])


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class MixerCmdTest(LinesMixin, unittest.TestCase):
    """cmd_mixer: values, mux, position, mixer.*, register write after the auto unmute."""

    def test_mixer_values_and_listing(self):
        for f in FLAVOURS:
            c2a = table_bytes(f, "data_flow_mix_cmd2array")
            out = self.lines(f, ["1|set.1 mixer.position=1", "1|get.1 mixer.position", "1|set.1 mixer.2.1.1=55",
                                 "1|set.1 mixer.3.1.10=3", "1|set.1 mixer.1.2=3", "1|set.1 mixer.1.1.9=3",
                                 "1|set.1 mixer.5.1.1=3", "1|set.1 mixer.position=5", "1|get.1 mixer.scale",
                                 "1|get.1 mixer.*"])
            self.assertIn("#.1 mixer.position=1", "\n".join(out), f)  # event of the position change
            out = [l for l in out if "#.1 mixer.position" not in l]
            self.assertEqual(out[1], "@0: <.1 mixer.position=1\\r\\n", f)
            self.assertIn(" ok", out[2], f)
            self.assertIn(" ok", out[3], f)  # 1.10 = input mux
            for i in (4, 5, 6, 7):
                self.assertIn("command out of range", out[i], f)
            self.assertEqual(out[8], "@0: <.1 mixer.scale=[0,100,1.0,0,8]\\r\\n", f)
            listing = out[9][4:].split("\\r\\n")[:-1]
            n = sum(1 for c in range(1, 11) for g in range(1, 11) if c2a[(c - 1) * 10 + g])
            self.assertEqual(len(listing), 4 * n, f)
            self.assertIn(" <.1 mixer.2.1.1=55", listing, f)
            self.assertIn(" <.1 mixer.3.1.10=3", listing, f)

    def test_mixer_register_after_unmute(self):
        f = "V41-96ELN-3"
        c2a = table_bytes(f, "data_flow_mix_cmd2array")
        cc = table_bytes(f, "data_flow_mix_can_change")
        regs = table_bytes(f, "data_flow_mix_dsp_reg")
        b = c2a[1]                      # mixer.P.1.1
        reg = regs[cc[b]]
        ii = (b - 1) % 4 + 1            # coefficient within the register
        out = self.lines(f, ["#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#dsp_detect",
                             "1|set.1 mixer.position=1"] + ["#mainloop"] * 12 +
                         ["1|set.1 mixer.1.1.1=50"] + ["#mainloop"] * 12 + [f"#block=1,68,{reg:02X},16"])
        blk = bytes.fromhex([l for l in out if l.startswith("@block:")][0][7:])
        # buffer sent backwards: coefficient 4 first, MSB first; 0.5 in 5.23 = 0x00400000
        k = (4 - ii) * 4
        self.assertEqual(blk[k:k + 4].hex(), "00400000")

    def test_mixer_ramp_clickless(self):
        """#50: with clickless on a mixer change ramps the row (0.05 per pass) and does not mute
        the DAC; without clickless the DAC is muted as in the original."""
        f = "V40-96ELN"
        c2a = table_bytes(f, "data_flow_mix_cmd2array")
        cc = table_bytes(f, "data_flow_mix_can_change")
        regs = table_bytes(f, "data_flow_mix_dsp_reg")
        b = c2a[(9 - 1) * 10 + 5]       # mixer.P.9.5
        reg = regs[cc[b]]
        k = (4 - ((b - 1) % 4 + 1)) * 4
        dac = table_bytes(f, "data_es9018_vol_reg")[1]
        pre = ["#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#i2cdev=2,90", "#dsp_detect",
               "1|set.1 mixer.position=1"] + ["#mainloop"] * 12
        for clickless, muted in ((1, False), (0, True)):
            out = self.lines(f, pre + [f"1|set.1 biquad.clickless={clickless}", "1|set.1 mixer.1.9.5=50",
                                       "#regs=2,90,16", "#mixramp", f"#block=1,68,{reg:02X},16"]
                             + ["#mixramp"] * 45 + ["#mainloop"] * 12 + [f"#block=1,68,{reg:02X},16"])
            r = [l for l in out if l.startswith("@regs:")][0][6:]
            dacv = r[2 * dac:2 * dac + 2]
            blks = [bytes.fromhex(l[7:]) for l in out if l.startswith("@block:")]
            self.assertEqual(dacv == "FF", muted, (clickless, dacv))
            if clickless:
                first = int.from_bytes(blks[0][k:k + 4], "big")
                self.assertTrue(0x18000 < first < 0x1C000, hex(first))      # 0.0125 after one pass
            self.assertEqual(blks[-1][k:k + 4].hex(), "00400000")           # 0.5 at the end


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class BiquadCmdTest(LinesMixin, unittest.TestCase):
    """cmd_biquad: stored values, ranges, calc and raw into DSP and DSP EEPROM."""

    PRE = ["#store_init", "#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#dsp_detect",
           "1|set.1 preset.loaded=0", "#mainloop"]

    def test_eeprom_write_verified(self):
        """#66: a write into the preset EEPROM is read back; lost writes are repeated, then counted (33)"""
        f = "V40-96ELN"
        ok = "\n".join(self.lines(f, self.PRE + ["1|set.1 biquad.3.2=[1,1000,30,707,0,128]", "1|get.1 system.errors"]))
        self.assertNotIn("[33,", ok)
        bad = "\n".join(self.lines(f, self.PRE + ["#ee_stuck=1,A8,1", "1|set.1 biquad.3.2=[1,1000,30,707,0,128]",
                                                  "1|get.1 system.errors"]))
        self.assertIn("[33,1]", bad)      # three attempts, then counted once

    def test_used_only(self):
        """#64: biquad.*.used sends only the places in use, then the lengths of the channels"""
        out = "\n".join(self.lines("V40-96ELN", ["1|set.1 biquad.3.2.calc=[1,1000,30,707,0,128]",
                                                 "1|set.1 biquad.35.1=[2,200,0,707,0,128]",
                                                 "1|get.1 biquad.*.used", "1|get.1 biquad.3.used", "1|get.1 biquad.*.x"]))
        lines = [l.strip() for l in out.replace("\\r\\n", "\n").split("\n") if "biquad." in l and "<.1" in l]
        self.assertEqual([l.split("<.1 ")[1] for l in lines], [
            "biquad.3.2=[1,1000,30,707,0,128]", "biquad.35.1=[2,200,0,707,0,128]",
            "biquad.used=[1,60,2,60,3,38,4,38,5,60,6,60,7,38,8,38,35,72,36,72]",
            "biquad.3.2=[1,1000,30,707,0,128]", "biquad.used=[3,38]"])
        self.assertIn("biquad.*.x unknown command", out)

    def test_values_ranges_listing(self):
        for f in FLAVOURS:
            limits = table_bytes(f, "data_flow_ch_bq_limits")
            used, = consts(f, "FLOW_MAX_CHANNELS_USED")
            fmax = int.from_bytes(table_bytes(f, "data_ranges_biquad_max_I")[4:6], "little", signed=True)
            ch = next(i for i in range(1, 65) if limits[i])
            bad = next(i for i in range(1, 65) if not limits[i])
            out = self.lines(f, ["#store_init", f"1|set.1 biquad.{ch}.1=[1]"] + self.PRE[1:] + [
                f"1|set.1 biquad.{ch}.2=[1,1000,-35,7071,0,128]", f"1|get.1 biquad.{ch}.2",
                f"1|set.1 biquad.{ch}.3=[1,1000,-35,7,0]", f"1|get.1 biquad.{ch}.3",
                f"1|set.1 biquad.{ch}.4=[1,30000,-35,707,0,0]", f"1|get.1 biquad.{ch}.4",
                "1|get.1 biquad", f"1|set.1 biquad.{bad}.1=[1]", f"1|set.1 biquad.{ch}.0=[1]",
                f"1|set.1 biquad.{ch}.{limits[ch] + 1}=[1]", f"1|get.1 biquad.{ch}"])
            txt = "\n".join(out)
            self.assertIn("command out of range", out[0], f)  # no preset loaded yet
            self.assertIn(f"<.1 biquad.{ch}.2=[1,1000,-35,7071,0,128]", txt, f)
            self.assertIn(f"<.1 biquad.{ch}.3=[1,1000,-35,700,0,128]", txt, f)    # old format: q/10, default flags
            self.assertIn(f"<.1 biquad.{ch}.4=[1,{fmax},-35,707,0,0]", txt, f)    # f clamped
            self.assertIn("<.1 biquad=[" + ",".join(str(limits[i]) for i in range(1, used + 1)) + "]", txt, f)
            self.assertEqual(txt.count("command out of range"), 4, f)
            self.assertEqual(txt.count(f"<.1 biquad.{ch}."), 3 + limits[ch], f)

    def test_calc_and_raw_to_dsp_and_eeprom(self):
        import tempfile
        f = "V41-96ELN-3"
        offs = table_words(f, "data_flow_ch_bq_offsets")
        core_t = table_bytes(f, "data_flow_dsp_bq_core")
        reg_t = table_bytes(f, "data_flow_dsp_bq_reg")
        soffs = table_words(f, "data_flow_bq_storage_offset")
        abq = table_words(f, "data_store_addr_bq")[0]
        adr = {1: (1, 0x68), 2: (2, 0x68), 3: (2, 0x6A)}
        limits = table_bytes(f, "data_flow_ch_bq_limits")
        picks = {}
        for ch in range(1, 65):
            for n in range(1, limits[ch] + 1):
                picks.setdefault(core_t[offs[ch] + n], (ch, n))
        self.assertEqual(sorted(picks), [1, 2, 3])
        for ch, n in picks.values():
            nr = offs[ch] + n
            core = core_t[nr]
            bus, a = adr[core]
            with tempfile.TemporaryDirectory() as d:
                eo = pathlib.Path(d, "ee.bin")
                pre = self.PRE + [f"#dspee_out={core},{eo}"]
                out = self.lines(f, pre + [f"1|set.1 biquad.{ch}.{n}.calc=[1,1000,35,1414,0,128]",
                                           f"#block={bus},{a:02X},{reg_t[nr]:02X},20",
                                           f'1|set.1 biquad.{ch}.{n}.raw=["0F000001","1","2","3","00800000"]',
                                           f"#block={bus},{a:02X},{reg_t[nr]:02X},20"])
                ee = eo.read_bytes()
            raw = bq_reference(f, 1, 1000, 35, 1414, 0, 0x80)  # A2, A1, B2, B1, B0
            data = b"".join(v.to_bytes(4, "big") for v in reversed(raw))
            blocks = [l[7:] for l in out if l.startswith("@block:")]
            self.assertEqual(blocks[0], data.hex().upper(), (ch, n))
            raw2 = [0x0F000001, 1, 2, 3, 0x00800000]
            data2 = b"".join(v.to_bytes(4, "big") for v in reversed(raw2))
            self.assertEqual(blocks[1], data2.hex().upper(), (ch, n))
            at = abq + (nr - soffs[core]) * 22
            self.assertEqual(ee[at:at + 22], xmodem(data2[::-1]).to_bytes(2, "big") + data2, (ch, n))


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class ClicklessTest(LinesMixin, unittest.TestCase):
    """#44: biquad.CH.N.calc fades in over intermediate filters instead of muting."""

    F = "V40-96ELN"
    PRE = BiquadCmdTest.PRE + ["1|set.1 biquad.clickless=1"]

    def block(self, ch, n):
        offs = table_words(self.F, "data_flow_ch_bq_offsets")
        core = table_bytes(self.F, "data_flow_dsp_bq_core")[offs[ch] + n]
        reg = table_bytes(self.F, "data_flow_dsp_bq_reg")[offs[ch] + n]
        bus, a = {1: (1, 0x68), 2: (2, 0x68), 3: (2, 0x6A)}[core]
        return f"#block={bus},{a:02X},{reg:02X},20"

    def ref(self, t, f, tenths, q1000):
        if t == 0:
            return "0080000000000000000000000000000000000000"
        raw = bq_reference(self.F, t, f, tenths, q1000, 0, 0x80)
        return b"".join(v.to_bytes(4, "big") for v in reversed(raw)).hex().upper()

    def run_cmds(self, cmds):
        out = self.lines(self.F, self.PRE + cmds)
        return out, [l[7:] for l in out if l.startswith("@block:")]

    def settle(self, n=80):
        return ["#bqramp"] * n

    def test_gain_ramp_without_mute(self):
        b = self.block(1, 1)
        out, blocks = self.run_cmds(
            ["1|set.1 biquad.1.1.calc=[1,1000,0,1414,0,128]"] + self.settle() + ["#mainloop"] * 200 + ["#mutechg",
             "1|set.1 biquad.1.1.calc=[1,1000,60,1414,0,128]", "#mutechg", b, "#bqramp", b, "#bqramp", b]
            + self.settle() + [b, "1|get.1 biquad.clickless", "1|get.1 biquad.1.1"])
        mute = [l for l in out if l.startswith("@mutechg:")]
        self.assertEqual(mute, ["@mutechg:0", "@mutechg:0"])          # no mute for a fadeable change
        self.assertEqual(blocks[0], self.ref(1, 1000, 0, 1414))       # DSP still has the old filter
        self.assertEqual(blocks[1], self.ref(1, 1000, 5, 1414))       # +0.5 dB per step
        self.assertEqual(blocks[2], self.ref(1, 1000, 10, 1414))
        self.assertEqual(blocks[3], self.ref(1, 1000, 60, 1414))      # end: exactly the target
        self.assertIn("biquad.clickless=1", out[-2])
        self.assertIn("biquad.1.1=[1,1000,60,1414,0,128]", out[-1])   # stored at once, as before

    def test_frequency_steps_and_type_change_via_0db(self):
        b = self.block(1, 2)
        out, blocks = self.run_cmds(
            ["1|set.1 biquad.1.2.calc=[1,1000,60,1414,0,128]"] + self.settle()
            + ["1|set.1 biquad.1.2.calc=[1,2000,60,1414,0,128]", "#bqramp", b] + self.settle() + [b]   # 1 octave
            + ["1|set.1 biquad.1.2.calc=[14,100,60,707,0,128]", "#bqramp", b] + self.settle() + [b])  # EQ -> shelf
        self.assertEqual(blocks[0], self.ref(1, 1000 * 2 ** (1 / 12), 60, 1414))   # 1/12 octave
        self.assertEqual(blocks[1], self.ref(1, 2000, 60, 1414))
        self.assertEqual(blocks[2], self.ref(1, 2000, 55, 1414))       # old EQ goes down first
        self.assertEqual(blocks[3], self.ref(14, 100, 60, 707))        # then the shelf comes up

    def test_end_is_bit_exact(self):
        """The last step writes exactly what a direct calc writes, also for odd values."""
        b = self.block(2, 4)
        for target in ((1, 1234, -37, 3333), (14, 87, 35, 707), (15, 9876, -61, 707)):
            t, f, g, q = target
            out, blocks = self.run_cmds([f"1|set.1 biquad.2.4.calc=[{t},100,0,707,0,128]"] + self.settle()
                                        + [f"1|set.1 biquad.2.4.calc=[{t},{f},{g},{q},0,128]"] + self.settle() + [b])
            self.assertEqual(blocks[-1], self.ref(t, f, g, q), target)

    def test_phase_angle(self):
        """#48: type 22 fades within one order; 1st <-> 2nd order (crossing 180 deg) mutes."""
        out, _ = self.run_cmds(
            ["1|set.1 biquad.1.4.calc=[22,80,0,707,45,128]"] + self.settle() + ["#mainloop"] * 200 + ["#mutechg",
             "1|set.1 biquad.1.4.calc=[22,80,0,707,120,128]", "#mutechg"] + self.settle() + ["#mainloop"] * 200
            + ["1|set.1 biquad.1.4.calc=[22,80,0,707,270,128]", "#mutechg"] + ["#mainloop"] * 200
            + ["1|set.1 biquad.1.4.calc=[21,300,0,707,0,128]"] + self.settle() + ["#mainloop"] * 200 + ["#mutechg",
             "1|set.1 biquad.1.4.calc=[21,80,0,707,0,128]", "#mutechg"])
        mute = [l for l in out if l.startswith("@mutechg:")]
        self.assertEqual(mute, ["@mutechg:0", "@mutechg:0", "@mutechg:255", "@mutechg:0", "@mutechg:0"])

    def test_fallbacks(self):
        b = self.block(1, 3)
        out, blocks = self.run_cmds(
            ["1|set.1 biquad.1.3.calc=[3,500,0,707,0,128]"] + self.settle() + ["#mainloop"] * 200 + ["#mutechg",
             "1|set.1 biquad.1.3.calc=[9,500,0,707,0,128]", "#mutechg", b,          # LP -> HP: mute, direct
             "1|set.1 biquad.clickless=0", "#mainloop"] + ["#mainloop"] * 200 + ["#mutechg",
             "1|set.1 biquad.1.3.calc=[9,800,0,707,0,128]", "#mutechg", b, "1|set.1 biquad.clickless=2"])
        mute = [l for l in out if l.startswith("@mutechg:")]
        self.assertEqual(mute, ["@mutechg:0", "@mutechg:255", "@mutechg:0", "@mutechg:255"])
        self.assertEqual(blocks[0], self.ref(9, 500, 0, 707))
        self.assertEqual(blocks[1], self.ref(9, 800, 0, 707))          # off: direct as the original
        self.assertIn("command out of range", out[-1])


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class EventTest(LinesMixin, unittest.TestCase):
    """libs/ctrl/events (pins, overflow, SPDIF receivers) and cmd_event / cmd_com."""

    def test_pins_and_overflow(self):
        for f in FLAVOURS:
            sd, dtr, gs1, ov = consts(f, "EVENTS_FIELD_SYSTEM_SHUTDOWN", "EVENTS_FIELD_EXT_USB_DTR",
                                     "EVENTS_FIELD_GROUND_SWITCH1", "EVENTS_FIELD_OVERFLOW_AD")
            out = self.lines(f, ["#pin=PIN_PERIPHERAL_AD_OVERFLOW,1", "#pin=PIN_CPU_POWER_CTRL_IN,1",
                                 "#events_init", "#events_poll", f"#event?{sd}",
                                 "#pin=PIN_CPU_POWER_CTRL_IN,0", "#pin=PIN_EXT_USB_DTR,1",
                                 "#pin=PIN_GROUND_SWITCH_2,1", "#events_poll", f"#event?{sd}", f"#event?{dtr}",
                                 f"#event?{gs1}", f"#event?{ov}",
                                 "#pin=PIN_PERIPHERAL_AD_OVERFLOW,0", "#events_poll", f"#event?{ov}",
                                 "#events_poll", f"#event?{ov}",  # no new edge
                                 "#pin=PIN_PERIPHERAL_AD_OVERFLOW,1", "#events_poll",
                                 "#events_overflow", f"#event?{ov}"])
            evs = [l for l in out if l.startswith("@event")]
            self.assertEqual(evs[0].split(":")[1].split(",")[0], "0", f)  # power in high: no shutdown
            self.assertEqual(evs[1].split(":")[1].split(",")[0], "1", f)  # power in low: shutdown
            self.assertEqual(evs[2].split(":")[1].split(",")[0], "1", f)  # usb dtr
            self.assertEqual(evs[3].split(":")[1].split(",")[0][-1:], "1", f)  # ground switch 2, bit 0
            v0 = table_bytes(f, "events_field_init_values")[ov - 1]   # default at sysinit
            v1 = ((v0 + 1) << 1) & 0xFF
            self.assertEqual(evs[4], f"@event{ov}:{v0},0", f)            # no overflow edge yet
            self.assertEqual(evs[5], f"@event{ov}:{v1},1", f)            # (v+1)<<1, inverted pin
            self.assertEqual(evs[6], f"@event{ov}:{v1},1", f)
            self.assertEqual(evs[7], f"@event{ov}:{v1 - 1},2", f)        # decay in the main loop
            p = subprocess.run([os.path.join(NATIVE, f"cmd-{f}")], input="\n".join([
                "#pin=PIN_PERIPHERAL_AD_OVERFLOW,1", "#events_init", "#pin=PIN_PERIPHERAL_AD_OVERFLOW,0",
                "#events_poll", "#pin=PIN_PERIPHERAL_AD_OVERFLOW,1", "#events_poll"]) + "\n",
                capture_output=True, text=True, timeout=10)
            self.assertIn("@pin:PIN_LED_RED=1\n", p.stdout, f)   # red LED while overflowing
            self.assertIn("@pin:PIN_LED_RED=0\n", p.stdout, f)

    def test_event_commands_and_com(self):
        f = "V41-96ELN-3"
        out = self.lines(f, ["#events_init", "1|set.1 event.70.value=5", "1|get.1 event.70.value",
                             "1|get.1 event.monitor=[70,0,0,200,0]", "1|set.1 event.block.1=[1,2,3,4]",
                             "1|get.1 event.monitor=[81,82,83,84,0]", "1|set.1 event.block.13=[1,2,3,4]",
                             "1|set.1 event.0.value=1", "1|set.1 event.129.value=1", "1|get.1 event.monitor",
                             "1|set.1 event.70.init=9", "1|get.1 event.70.init", "1|set.1 com.3=hello",
                             "1|set.1 com.4=x", "1|set.1 com.2=x"])
        self.assertEqual(out[0], "@0: *.1 event.70.value ok\\r\\n")
        self.assertEqual(out[1], "@0: <.1 event.70.value=5\\r\\n")
        self.assertEqual(out[2], "@0: #.1 event.monitor=[5,0,0,0,0]\\r\\n")
        self.assertEqual(out[3], "@0: #.1 event.monitor=[1,2,3,4,0]\\r\\n")  # block: no answer
        for i in (4, 5, 6):
            self.assertIn("command out of range", out[i])
        self.assertIn("unknown para", out[7])
        self.assertEqual(out[9], "@0: <.1 event.70.init=9\\r\\n")
        self.assertEqual(out[10], "@1:hello\\r\\n")   # #3 = COM2 = RS485 master
        self.assertEqual(out[11], "@3:x\\r\\n")       # #4 = COM4 = RS485 slave
        self.assertIn("unknown command", out[12])     # #2 only with BT720

    def test_spdif_ct7302_scan(self):
        f = "V41-96ELN-3"
        r1, r2, r3, r4 = consts(f, "EVENTS_FIELD_SPDIF_REC_1", "EVENTS_FIELD_SPDIF_REC_2",
                                "EVENTS_FIELD_SPDIF_REC_3", "EVENTS_FIELD_SPDIF_REC_4")
        out = self.lines(f, ["#i2cdev=1,20,122=0", "#i2cdev=1,22,122=0", "#spdif_init", "#events_init"] +
                         ["#events_spdif"] * 6 + [f"#event?{n}" for n in (r1, r2, r3, r4)] + ["#regs=1,22,8"])
        vals = [l.split(":")[1].split(",")[0] for l in out if l.startswith("@event")]
        self.assertEqual(vals, ["1", "1", "1", "1"])  # silence register 0: valid PCM on every input
        regs = bytes.fromhex([l for l in out if l.startswith("@regs:")][0][6:])
        # 6 steps: mux 1(->5 over I2S),2,3,4,0,1(->5): ends on RX5
        self.assertEqual(regs[4] & 0x07, 0x05)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class ModcTest(LinesMixin, unittest.TestCase):
    """libs/ctrl/modc/v20 and cmd_modc. Block output n = event 96 + n."""

    PRE = ["#pin=PIN_CPU_POWER_CTRL_IN,1", "#pin=PIN_PERIPHERAL_AD_OVERFLOW,1", "#events_init", "#events_poll",
           "#modc_init"]

    def val(self, out, n):
        return int([l for l in out if l.startswith(f"@event{n}:")][-1].split(":")[1].split(",")[0])

    def test_defaults_listing_and_config(self):
        for f in FLAVOURS:
            en = table_bytes(f, "modc_enable_default")
            ty = table_bytes(f, "modc_type_default")
            out = self.lines(f, self.PRE + ["1|get.1 modc.*", "1|get.1 modc.32", "1|set.1 modc.32.type=0",
                                            "1|set.1 modc.0.type=0", "1|set.1 modc.33.type=0",
                                            "1|set.1 modc.7=[1,48,03000000,70,71,0,0,01020000]", "1|get.1 modc.7",
                                            "1|set.1 modc.7.sets.2=9", "1|get.1 modc.7.sets", "1|get.1 modc.7.flags.1",
                                            "1|set.1 modc.7.type=63", "1|get.1 modc.7", "1|set.1 modc.7=[1,2]"])
            txt = "\n".join(out)
            self.assertEqual(txt.count("<.1 modc.") - txt.count("<.1 modc.7") - 1, sum(1 for t in ty if t), f)
            self.assertIn(f"<.1 modc.32=[{en[31]},{ty[31]},", out[1], f)
            for i in (2, 3, 4):
                self.assertIn("command out of range", out[i], f)       # block 32 is fixed, 0/33 invalid
            self.assertIn("<.1 modc.7=[1,48,03000000,70,71,0,0,01020000]", txt, f)
            self.assertIn("<.1 modc.7.sets=[1,9,0,0]", txt, f)
            self.assertIn("<.1 modc.7.flags.1=3", txt, f)
            self.assertIn("<.1 modc.7=[0,0,00000000,0,0,0,0,00000000]", txt, f)  # unknown type -> empty
            self.assertIn("unknown para", out[-1], f)

    def test_config_is_stored_in_eeprom(self):
        f = "V41-96ELN-3"
        img = bytearray(image(f))
        p, ee = run(f, ["1|set.1 modc.7=[1,48,03000000,70,71,0,0,01020000]"], eeprom=bytes(img), want_eeprom=True)
        import re
        t = pathlib.Path(HERE, "..", "src", "flavours", f, "flavour_eram.h").read_text()
        a = {n: int(re.search(rf"#define ERAM_MODC_{n}_ERAM (\d+)", t).group(1)) for n in
             ("ENABLE", "TYPE", "FLAGS", "EVENTS", "SETS")}
        self.assertEqual(ee[a["ENABLE"] + 6], 1)
        self.assertEqual(ee[a["TYPE"] + 6], 48)
        self.assertEqual(ee[a["FLAGS"] + 6], 3)
        self.assertEqual([ee[a["EVENTS"] + 6], ee[a["EVENTS"] + 38]], [70, 71])
        self.assertEqual([ee[a["SETS"] + 6], ee[a["SETS"] + 38]], [1, 2])

    def test_shutdown_block(self):
        for f in FLAVOURS:
            sets = table_bytes(f, "modc_sets_default")
            s2, s3, s4 = sets[32 + 31], sets[64 + 31], sets[96 + 31]
            out = self.lines(f, self.PRE + ["#modc"] * 3 + ["#pin=PIN_CPU_POWER_CTRL_IN,0", "#events_poll"] +
                             ["#modc"] * (s2 + 2))
            txt = "\n".join(out)
            self.assertEqual(txt.count("@shutdown:"), 1, f)
            self.assertIn(f"@shutdown:{s3},{s4}", txt, f)       # waits before remote off / power off
            self.assertIn("#.1 system.shutdown=ok", txt, f)
            # power in stays high: nothing
            out = self.lines(f, self.PRE + ["#modc"] * 10)
            self.assertNotIn("@shutdown", "\n".join(out), f)

    def test_logic_toggle_select_compare_calculator_limiter(self):
        f = "V41-96ELN-3"
        pre = self.PRE + [
            "1|set.1 modc.7=[1,48,00000000,70,71,0,0,03000000]",    # logic: 70 AND-mask 3 -> any of 70/71
            "1|set.1 modc.8=[1,50,00000000,72,0,0,0,00000000]",     # toggle on falling 72
            "1|set.1 modc.9=[1,49,00000000,73,74,75,0,00000000]",   # select 73/74 by 75
            "1|set.1 modc.10=[1,52,01000000,73,74,75,0,00000000]",  # compare: 75 >= 73 -> 74 else 73
            "1|set.1 modc.11=[1,53,00840000,73,74,0,0,00000000]",   # calculator: 73 + 74
            "1|set.1 modc.12=[1,51,00000000,73,0,0,0,C8006400]",    # limiter: 0..200 -> 0..100
        ]
        steps = ["1|set.1 event.70.value=1", "1|set.1 event.72.value=1", "1|set.1 event.73.value=50",
                 "1|set.1 event.74.value=20", "#modc", "#modc", "1|set.1 event.72.value=0", "#modc", "#modc"]
        out = self.lines(f, pre + steps + [f"#event?{96 + n}" for n in range(7, 13)])
        self.assertEqual([self.val(out, 96 + n) for n in range(7, 13)], [1, 1, 50, 50, 70, 25])
        out = self.lines(f, pre + steps + ["1|set.1 event.70.value=0", "1|set.1 event.75.value=60",
                                           "1|set.1 event.72.value=1", "1|set.1 event.72.value=0", "#modc", "#modc"] +
                         [f"#event?{96 + n}" for n in range(7, 13)])
        self.assertEqual([self.val(out, 96 + n) for n in (7, 9, 10)], [0, 20, 20])

    def test_set_type_clears_only_own_output(self):
        """#38: the original also cleared fields n, n+32, n+64; the port only the output 96+n."""
        f = "V40-96ELN"
        out = self.lines(f, self.PRE + ["1|set.1 event.40.value=7", "1|set.1 event.72.value=9",
                                        "1|set.1 modc.8=[1,16,0,40,0,0,0,0]", "#modc",
                                        "1|set.1 modc.8.type=17", "#event?8", "#event?40", "#event?72", "#event?104"])
        self.assertEqual([self.val(out, n) for n in (40, 72, 104)], [7, 9, 0])

    def test_mute_volumen_mix_preset(self):
        f = "V41-96ELN-3"
        volreg = table_bytes(f, "data_es90x8pro_vol_reg")
        ovd = table_bytes(f, "data_flow_dsp_output_vol_dsp")
        ovr = table_bytes(f, "data_flow_dsp_output_vol_reg")
        vol = table_bytes(f, "data_dsp_vol")
        adr = {1: (1, 0x68), 2: (2, 0x68), 3: (2, 0x6A)}
        bus, a = adr[ovd[1]]
        out = self.lines(f, ["#i2cdev=2,90", "#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#dsp_detect", "#dac_init"] +
                         self.PRE + [
            "1|set.1 modc.6=[1,84,05000000,70,0,0,0,00000000]",     # mute outputs 1+3 while event 70
            "1|set.1 modc.7=[1,16,FF000000,71,71,0,0,00000000]",    # volumen: all outputs, event 71
            "1|set.1 modc.8=[1,97,00000000,0,72,0,0,00000000]",     # mixer position 2 while event 72
            "1|set.1 gain.out.1=200", "1|set.1 event.71.value=245", "1|set.1 event.70.value=1",
            "1|set.1 event.72.value=1"] + ["#mainloop"] * 12 +   # incl. the auto unmute after the mixer change
            [f"#block={bus},{a:02X},{ovr[1]:02X},4", "#regs=2,90,24",
            "1|get.1 mixer.position", "1|get.1 gain.out.1"])
        txt = "\n".join(out)
        self.assertIn(f"@block:{vol[190 * 4:190 * 4 + 4][::-1].hex().upper()}", txt)   # 200 - (255 - 245)
        self.assertIn("#.1 gain.out.state=[190,0,0,0,0,0,0,0]", txt)   # event to the GUI
        regs = bytes.fromhex([l for l in out if l.startswith("@regs:")][0][6:])
        self.assertEqual([regs[volreg[ch]] for ch in range(1, 9)], [0xFF, 0, 0xFF, 0, 0, 0, 0, 0])
        self.assertIn("<.1 mixer.position=2", txt)
        self.assertIn("<.1 gain.out.1=200", txt)     # the stored value stays


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class AdcGainTest(LinesMixin, unittest.TestCase):
    """events/adc (averages, peak, overheat mute) and the MODC gain / gain_quad blocks."""

    def test_adc_averages_limits_overheat(self):
        out = self.lines("V40-96ELN", ["#adc_init", "#adc=0,0,1000", "#adc=0,1,2047", "#adc=1,0,2000", "#adc_loop",
                                        "#adc_41", "#adc=1,0,4000", "#adc_loop", "#adc_41", "#adc=1,0,500",
                                        "#adc_loop"])
        t85 = 0x0A00 - 204                              # harness: TEMPSENSE = 0x0A00
        corr = (273.15 + 85.0) / t85
        lim = [int((273.15 + d) / corr) for d in (-40.0, 90.0, 95.0, 100.0)]
        self.assertEqual(out[0], f"@adc_limits:{lim[0]},{t85},{lim[1]},{lim[2]},{lim[3]}")
        self.assertEqual(out[1], "@adc:1000,2047,1796,1796")
        self.assertEqual(out[2], "@unmute:0")               # below 85 °C
        self.assertEqual(out[3], "@adc:1000,2047,3796,3796")
        self.assertEqual(out[4], "@unmute:80")              # overheated: muted for 80 API passes
        self.assertEqual(out[5], "@adc:1000,2047,3796,296")  # peak (value 3) stays

    def ev(self, out, n):
        return int([l for l in out if l.startswith(f"@event{n}:")][-1].split(":")[1].split(",")[0])

    def test_gain_block(self):
        f = "V40-96ELN"
        log10 = table_bytes(f, "modc_log10")
        pre = ["#pin=PIN_CPU_POWER_CTRL_IN,1", "#events_init", "#modc_init", "#adc_init",
               "1|set.1 modc.9=[1,17,00000000,0,0,0,0,F0100F00]"]   # set1 240, set2 16, set3 15 steps
        for raw, flags in ((1000, "00"), (1500, "08"), (300, "00")):
            out = self.lines(f, pre + [f"1|set.1 modc.9.flags.1={int(flags, 16)}", f"#adc=0,0,{raw}", "#adc_loop",
                                       "#modc", "#event?105"])
            w1 = (raw << 5) & 0xFFFF
            a = ((w1 - (16 << 8)) & 0xFFFF) // (240 - 16) & 0xFF
            if flags == "08":
                a = log10[a]
            self.assertEqual(self.ev(out, 105), a // (255 // 15), (raw, flags))

    def test_gain_quad_block(self):
        f = "V40-96ELN"
        out = self.lines(f, ["#pin=PIN_CPU_POWER_CTRL_IN,1", "#events_init", "#modc_init",
                             "1|set.1 modc.9=[1,82,80000000,0,0,0,0,32040A05]", "#modc", "#rotary?", "#event?105",
                             "#rotary=40", "#modc", "#event?105"])
        self.assertIn("@rotary:50,4,5,10", out)  # sets: start 50, step 4, max 10, min 5
        self.assertEqual(self.ev(out, 105), 255 - 40)  # flag1 bit 7: inverted


def nec_samples(address, command, header_pulse=135):
    """pulse/space lengths in TCC1 samples (66.75 us): 9 ms + 4.5 ms header, 32 bits MSB first
    per 16-bit frame, stop pulse and a long space."""
    seq = [header_pulse, 67]
    for word in (address, command):
        for b in range(15, -1, -1):
            seq += [8, 25 if (word >> b) & 1 else 8]
    seq += [8, 60]
    return ",".join(map(str, seq))


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class IrTest(LinesMixin, unittest.TestCase):
    """libs/extender/ir-receiver and cmd_extender."""

    def test_nec_gain_repeat_and_echo(self):
        f = "V40-96ELN"
        out = self.lines(f, ["#ir_init", f"#irbuf={nec_samples(0x00FF, 0xD0CC)}", "#ir_loop",
                             "#irbuf=135,33,8,200", "#ir_loop",                       # NEC repeat: last frame again
                             f"#irbuf={nec_samples(0x00FF, 0xB0CC)}", "#ir_loop",
                             "#irbuf=20,20,20,20", "#ir_loop"])                        # noise: no action
        import re
        ir = ["@ir:" + m for m in re.findall(r"@ir:([^\n]*)", "\n".join(out))]
        self.assertEqual(ir[0], f"@ir:0,{0x00FF},{0xD0CC} value=1")   # incr gain
        self.assertEqual(ir[1], f"@ir:0,{0x00FF},{0xD0CC} value=2")   # repeat
        self.assertEqual(ir[2], f"@ir:0,{0x00FF},{0xB0CC} value=1")   # decr gain
        self.assertTrue(ir[3].endswith("value=1"))
        self.assertIn(f"#.1 extender.ir.analyze=[0,{0x00FF},{0xD0CC}]", "\n".join(out))

    def test_custom_codes_and_commands(self):
        f = "V40-96ELN"
        out = self.lines(f, ["#ir_init", "1|set.1 extender.ir.custom.3=[0,4660,22136]",
                             "1|get.1 extender.ir.custom.3", "1|set.1 extender.ir.custom.19=[1,2,3]",
                             "1|set.1 extender.ir.analyze.echo=0", "1|get.1 extender.ir.analyze.echo",
                             "1|get.1 extender.ir.state", "1|set.1 system.debug.mode=0"])
        self.assertEqual(out[1], "@0: <.1 extender.ir.custom.3=[0,4660,22136]\\r\\n")
        self.assertIn("command out of range", out[2])
        self.assertEqual(out[4], "@0: <.1 extender.ir.analyze.echo=0\\r\\n")
        # custom mode (setup bit 0) with code 3 = incr gain
        out = self.lines(f, ["#ir_init", "1|set.1 extender.ir.custom.3=[0,4660,22136]",
                             "1|set.1 extender.ir.analyze.echo=0", "#reg=11,1",
                             f"#irbuf={nec_samples(4660, 22136)}", "#ir_loop"])
        self.assertIn("value=1", "\n".join(out))

    def test_custom_2_next_input(self):
        """#27: custom command 2 switches to the next input and wraps around."""
        f = "V40-96ELN"
        mx = int(self.lines(f, ["1|get.1 mixer.in.max"])[0].split("=")[1].split("\\")[0])
        code = ["1|set.1 extender.ir.custom.2=[0,4660,22136]", "1|set.1 extender.ir.analyze.echo=0", "#reg=11,1"]
        press = [f"#irbuf={nec_samples(4660, 22136)}", "#ir_loop"]
        out = self.lines(f, ["#events_init", "#dtr=1", "#ir_init"] + code + ["1|set.1 mixer.position=1", "1|set.1 mixer.in=2"]
                         + press + ["1|get.1 mixer.in", "1|set.1 mixer.in=" + str(mx)] + ["#ir_loop"] * 192 + press
                         + ["1|get.1 mixer.in"])
        got = [l for l in out if "<.1 mixer.in=" in l]
        self.assertIn("mixer.in=3\\r", got[0])     # 2 -> 3
        self.assertIn("mixer.in=1\\r", got[1])     # last input -> 1



@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class I2cCmdTest(LinesMixin, unittest.TestCase):
    """cmd_i2c: register read/write, dump, buffer, scan, state; EEPROMs refused."""

    def test_i2c_commands(self):
        f = "V40-96ELN"
        out = self.lines(f, ["#i2cdev=2,90,4=71,5=01", "#i2cdev=1,68", "1|get.1 i2c.2.144.4",
                             "1|set.1 i2c.2.144.6=85", "1|get.1 i2c.2.144.dump=[4,6]", "1|get.1 i2c.2.144.buffer=[4,3]",
                             "1|get.1 i2c.1.scan", "1|get.1 i2c.1.state", "1|set.1 i2c.1.160.0=1",
                             "1|get.1 i2c.1.168.0", "1|get.1 i2c.3.144.4", "1|get.1 i2c.2.144"])
        self.assertEqual(out[0], "@0: <.1 i2c.2.144.4=&H71,&B01110001\\r\\n")
        self.assertEqual(out[1], "@0: *.1 i2c.2.144.6 ok\\r\\n")
        self.assertEqual(out[2].split("\\r\\n")[2], " <.1 i2c.2.144.dump=&H06;&H55,&B01010101")
        self.assertEqual(out[3], "@0: <.1 i2c.2.144.buffer=550171\\r\\n")   # BASCOM buffer order
        self.assertIn("&H68", out[4])
        self.assertEqual(out[5], "@0: <.1 i2c.1.state=1, 1, &H01, &B00000001\\r\\n")
        for i in (6, 7, 8):
            self.assertIn("command out of range", out[i])                    # EEPROMs, bus 3
        self.assertIn("unknown command", out[9])

    def test_set_buffer(self):
        """PORT (#51): set i2c.B.A.buffer=[r,b1..bn], b1 first on the bus; EEPROMs refused."""
        f = "V40-96ELN"
        out = self.lines(f, ["#i2cdev=2,90,4=71", "1|set.1 i2c.2.144.buffer=[6,1,2,3]",
                             "1|get.1 i2c.2.144.buffer=[6,3]", "1|set.1 i2c.1.160.buffer=[0,1]",
                             "1|set.1 i2c.2.144.buffer=[6]"])
        self.assertEqual(out[0], "@0: *.1 i2c.2.144.buffer ok\\r\\n")
        self.assertEqual(out[1], "@0: <.1 i2c.2.144.buffer=030201\\r\\n")   # regs 6,7,8 = 1,2,3
        self.assertIn("command out of range", out[2])
        self.assertIn("command out of range", out[3])


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class ModcStoreTest(unittest.TestCase):
    """MODC mainclock and ess9038pro write their settings to the EEPROM like the original."""

    def addr(self, f, name):
        import re
        t = pathlib.Path(HERE, "..", "src", "flavours", f, "flavour_eram.h").read_text()
        return int(re.search(rf"#define ERAM_{name} (\d+)", t).group(1))

    def test_mainclock(self):
        f = "V40-96ELN"
        clk = table_words(f, "data_modc_mainclock")
        p, ee = run(f, ["#pin=PIN_CPU_POWER_CTRL_IN,1", "#events_init", "#modc_init",
                        "1|set.1 modc.9=[1,68,00000000,0,0,0,0,F4010300]", "#modc"],
                    eeprom=image(f), want_eeprom=True)
        fine = self.addr(f, "SI5338_CUSTOM_MCLK_FINE_TUNE")
        out = self.addr(f, "SI5338_OUT_CLOCK_ERAM") + 3 * 4
        self.assertEqual(int.from_bytes(ee[fine:fine + 2], "little"), 0x01F4)
        self.assertEqual(int.from_bytes(ee[out:out + 4], "little"), clk[6] | clk[7] << 16)  # index 3

    def test_ess9038pro(self):
        f = "V41-96ELN-3"
        reg = self.addr(f, "ES90X8PRO_FB_SYSMUTE_ERAM")
        init, = consts(f, "ES90X8PRO_FB_SYSMUTE_INIT")
        p, ee = run(f, ["#pin=PIN_CPU_POWER_CTRL_IN,1", "#events_init", "#modc_init",
                        "1|set.1 modc.9=[1,79,00000000,0,0,0,0,05000000]", "#modc"],
                    eeprom=image(f), want_eeprom=True)
        self.assertEqual(ee[reg], 5)
        p, ee = run(f, ["#pin=PIN_CPU_POWER_CTRL_IN,1", "#events_init", "#modc_init",
                        "1|set.1 modc.9=[1,79,00000000,0,0,0,0,05000000]", "#modc", "1|set.1 modc.9.enable=0",
                        "#modc"], eeprom=image(f), want_eeprom=True)
        self.assertEqual(ee[reg], init)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class SystemRestTest(LinesMixin, unittest.TestCase):
    """system.cfg.change, feature.attributes, gui.panes, poti, temperature."""

    def test_system_rest(self):
        for f in FLAVOURS:
            out = self.lines(f, ["#adc_init", "#adc=1,0,2800", "#adc=0,1,1234", "#adc_loop",
                                 "1|get.1 system.temperature.celsius", "1|get.1 system.poti.2",
                                 "1|get.1 system.feature.attributes", "1|get.1 system.feature.attributes.fr",
                                 "1|get.1 system.feature.attributes.hw", "1|get.1 system.gui.panes",
                                 "1|get.1 system.gui.panes.sy", "1|set.1 system.cfg.change=off",
                                 "1|set.1 system.cfg.change=7", "1|get.1 system.feature.attributes.xx"])[2:]
            t85 = 0x0A00 - 204
            c = (2800 - 204) * ((273.15 + 85.0) / t85) - 273.15
            self.assertEqual(out[0], f"@0: <.1 system.temperature.celsius={c:.2f}\\r\\n", f)
            self.assertEqual(out[1], "@0: <.1 system.poti.2=1234\\r\\n", f)
            self.assertIn('=["fr","hw","oa","ob"]', out[2], f)
            self.assertIn("system.feature.attributes.fr=[10,22000]", out[3], f)
            hw = consts(f, "CHK_CHANNEL_OPEN", "CHK_HARDWARE_DSP", "CHK_HARDWARE_OTHER")
            self.assertIn(f"system.feature.attributes.hw=[{hw[0]},", out[4], f)
            self.assertIn(f",{hw[1]},", out[4], f)
            self.assertIn('=["in","bq","ou","sy"]', out[5], f)
            self.assertIn("system.gui.panes.sy=[", out[6], f)
            self.assertIn("ok", out[7], f)
            self.assertIn("unknown para", out[8], f)
            self.assertIn("unknown command", out[9], f)

    def test_temperature_out_of_range(self):
        out = self.lines("V40-96ELN", ["#adc_init", "#adc=1,0,300", "#adc_loop", "1|get.1 system.temperature"])
        self.assertEqual(out[-1], "@0: <.1 system.temperature=nop\\r\\n")


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class FactoryResetTest(unittest.TestCase):
    """Factory reset (sysinit) of the original, FORGE_FACTORY_RESET=1 (#factory=1 in the harness)."""

    def eram(self, f, name):
        import re
        t = pathlib.Path(HERE, "..", "src", "flavours", f, "flavour_eram.h").read_text()
        return int(re.search(rf"#define ERAM_{name} (\d+)", t).group(1))

    def run_at24(self, f, lines, at24):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            d = pathlib.Path(d)
            (d / "a.bin").write_bytes(bytes(at24))
            for n in (1, 2, 3):
                (d / f"e{n}.bin").write_bytes(b"\x55" * 65536)
            pre = [f"#at24={d / 'a.bin'}", f"#at24out={d / 'o.bin'}"]
            for n in (1, 2, 3):
                pre += [f"#dspee={n},{d / f'e{n}.bin'}", f"#dspee_out={n},{d / f'eo{n}.bin'}"]
            p, ee = run(f, pre + lines, want_eeprom=True)   # blank internal EEPROM: sysinit
            return p, ee, (d / "o.bin").read_bytes()

    def test_preset_eeprom_erase_and_defaults(self):
        import random
        f = "V40-96ELN"
        at24 = bytearray(random.Random(3).randrange(256) for _ in range(65536))
        p, ee, o = self.run_at24(f, ["#factory=1", "#at24_detect"], at24)
        self.assertEqual(o, b"\xff" * 65536)                       # eeprom_clear_mem
        p, ee, o = self.run_at24(f, ["#factory=1", "#at24_detect", "#store_init"], at24)
        fs, cb, nmax = consts(f, "STORAGE_FRAMESIZE", "STORAGE_CHKSUM_BASE", "MODUL_MAX_PRESET")
        vin_b, vin_d = consts(f, "STORAGE_VOL_IN_BASE", "STORE_VOL_IN_DEFAULT")
        av = table_words(f, "data_store_addr_values")
        for n in range(nmax):
            fr = o[av[n]:av[n] + fs]
            self.assertEqual(fr[vin_b - 1], vin_d, n)
            self.assertEqual(int.from_bytes(fr[cb - 1:cb + 1], "little"), xmodem(fr[:cb - 1]), n)
        reg, = consts(f, "REG_LOAD_PRESET")
        self.assertEqual(ee[self.eram(f, "REGISTERS_ERAM") + reg - 1], 0)   # store_set_preset

    def test_without_flag_nothing_is_erased(self):
        import random
        f = "V40-96ELN"
        at24 = bytearray(random.Random(3).randrange(256) for _ in range(65536))
        p, ee, o = self.run_at24(f, ["#at24_detect", "#store_init", "#events_init", "#modc_init", "#ir_init"], at24)
        self.assertEqual(o, bytes(at24))
        self.assertEqual(ee[self.eram(f, "MODC_TYPE_ERAM"):self.eram(f, "MODC_TYPE_ERAM") + 32], b"\xff" * 32)

    def test_internal_defaults(self):
        f = "V40-96ELN"
        p, ee = run(f, ["#factory=1", "#events_init", "#modc_init", "#ir_init", "#sys_init_init"], want_eeprom=True)
        a = self.eram(f, "EVENTS_FIELD_INIT_ERAM")
        self.assertEqual(ee[a:a + 128], table_bytes(f, "events_field_init_values"))
        a = self.eram(f, "MODC_TYPE_ERAM")
        self.assertEqual(ee[a:a + 32], table_bytes(f, "modc_type_default"))
        a = self.eram(f, "MODC_SETS_ERAM")
        self.assertEqual(ee[a:a + 128], table_bytes(f, "modc_sets_default"))
        a = self.eram(f, "IRRECEIVER_CUSTOM_HEADER_ERAM")
        self.assertEqual(ee[a:a + 36], b"\xff" * 36)
        reg, = consts(f, "REG_EXT_IRSETUP")
        self.assertEqual(ee[self.eram(f, "REGISTERS_ERAM") + reg - 1], 0)
        a = self.eram(f, "SYS_INIT_SETS_ERAM")
        self.assertEqual(ee[a], 0)                                    # "" = empty string

    def test_reinit_and_memory_commands(self):
        f = "V40-96ELN"
        st, = consts(f, "REG_STARTUP_STATE")
        p, ee = run(f, ["1|set.1 system.reinit"], eeprom=image(f), want_eeprom=True)
        self.assertIn("unknown command", p.stdout)                   # without the flag
        p, ee = run(f, ["#factory=1", "1|set.1 system.reinit"], eeprom=image(f), want_eeprom=True)
        self.assertIn("*.1 system.reinit ok", p.stdout)
        self.assertIn("@reset", p.stdout)
        a = self.eram(f, "REGISTERS_ERAM")
        self.assertEqual(ee[a + st - 1], 255)                         # startup_state_sysinit
        p, ee = run(f, ["#factory=1", "1|set.1 system.hardware.memory.eram"], eeprom=image(f), want_eeprom=True)
        self.assertIn("system.hardware.memory.eram=ok(&H0F)", p.stdout)
        self.assertIn("system.hardware.memory.eram=ok(&HF0)", p.stdout)
        self.assertIn("@reset", p.stdout)
        self.assertEqual(ee[a + st - 1], 255)
        self.assertEqual(ee[:a], b"\xf0" * a)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class InputSelectTest(LinesMixin, unittest.TestCase):
    """Port extension #23: mixer.in (input = mux of the active position), events 45..48."""

    def test_input_select(self):
        for f in FLAVOURS:
            ct = "ct7302" if f != "V30-96LN" else "src43x2"
            tab = table_bytes(f, f"modul_{ct}_tab_set_mux")
            vmax = max(i for i in range(1, len(tab)) if tab[i] != 0xFF)
            out = self.lines(f, ["#events_init", "#dtr=1", "1|set.1 mixer.position=2", "1|get.1 mixer.in.max",
                                 "1|set.1 mixer.in=3", "#event?46", "1|get.1 mixer.in", "1|get.1 mixer.2.1.10",
                                 f"1|set.1 mixer.in={vmax + 1}", "1|set.1 mixer.in=0", "1|set.1 mixer.1.1.10=2",
                                 "#event?45"])
            txt = "\n".join(out)
            self.assertIn(f"<.1 mixer.in.max={vmax}", txt, f)
            self.assertIn("#.1 mixer.in=3", txt, f)                       # event to the GUI
            self.assertIn("@event46:3,", txt, f)                           # SOURCE.2.MUX
            self.assertIn("<.1 mixer.in=3", txt, f)
            self.assertEqual(txt.count("command out of range"), 2, f)      # vmax+1, 0
            self.assertIn("mixer.2.1.10 unknown command", txt, f)          # get of a value: as the original
            self.assertIn("@event45:2,", txt, f)                           # mux of position 1 via mixer.1.1.10

    def test_input_reaches_the_receiver(self):
        f = "V40-96ELN"
        tab = table_bytes(f, "modul_ct7302_tab_set_mux")
        out = self.lines(f, ["#i2cdev=1,20,4=0", "#i2cdev=1,22", "#spdif_init", "#events_init",
                             "#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#dsp_detect",
                             "1|set.1 mixer.position=1"] + ["#mainloop"] * 12 +
                         ["1|set.1 mixer.in=5"] + ["#mainloop"] * 12 + ["#regs=1,20,8"])
        regs = bytes.fromhex([l for l in out if l.startswith("@regs:")][-1][6:])
        self.assertEqual(regs[4] & 0x07, tab[5])   # input 5 = coaxial (RX4) after the auto unmute


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class SdPresetTest(unittest.TestCase):
    """#24: GUI .cfg files from the microSD into the presets (libs/file/preset_load)."""

    def run_sd(self, f, files, steps, at24=None):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            d = pathlib.Path(d)
            lines = [f"#sd_mkfs={d / 'sd.img'},8"]
            for name, text, date in files:
                (d / name).write_text(text)
                lines.append(f"#sd_put={name},{d / name}" + ("," + ",".join(map(str, date)) if date else ""))
            if at24 is not None:
                (d / "a.bin").write_bytes(bytes(at24))
                lines = [f"#at24={d / 'a.bin'}", f"#at24out={d / 'o.bin'}"] + lines
            p, ee = run(f, lines + steps, want_eeprom=True)
            o = (d / "o.bin").read_bytes() if at24 is not None else None
        return p.stdout, ee, o

    def test_load_store_and_skip_unchanged(self):
        for f in ("V40-96ELN", "V30-96LN"):
            model = {"V40-96ELN": "96ELN", "V30-96LN": "96LN"}[f]
            cfg = (f"[{model}]\n" 'FILE_v10 = "gain.out.1=111"\n' 'FILE_v10 = "gain.out.2=122"\n'
                   'FILE_v10 = "mute.out.3=1"\n')
            files = [("preset_1.cfg", cfg, (2026, 11, 11, 18, 30)), ("preset_2.cfg", cfg.replace("111", "99"), None)]
            out, ee, o = self.run_sd(f, files, ["#sd_card=1", "#sd_init", "#sd_load", "#sd_load"],
                                     at24=b"\xff" * 65536)
            self.assertIn("@sd_ready:1", out, f)
            self.assertEqual(out.count("*.1 gain.out.1 ok"), 2, f)       # both files once, second run skipped
            self.assertIn("#.1 preset.erase=0", out, f)
            self.assertIn("#.1 preset.erase=1", out, f)
            self.assertIn("@sd_loaded preset=255", out, f)              # MODC loads the selected preset
            fs, vout = consts(f, "STORAGE_FRAMESIZE", "STORAGE_VOL_OUT_BASE")
            av = table_words(f, "data_store_addr_values")
            self.assertEqual(list(o[av[0] + vout - 1:av[0] + vout + 1]), [111, 122], f)  # saved to preset 0
            self.assertEqual(list(o[av[1] + vout - 1:av[1] + vout + 1]), [99, 122], f)   # and preset 1
            self.assertIn(b"11.11.26 18:30:00", ee, f)                  # date stored in the EEPROM

    def test_diagnostics(self):
        """get system.sd / system.sd.files (read-only); a card inserted later is mounted on request"""
        f = "V40-96ELN"
        files = [("preset_1.cfg", "[96ELN]\n", None), ("notes.txt", "x" * 1234, None)]
        out, _, _ = self.run_sd(f, files, ["1|get.1 system.sd", "1|get.1 system.sd.files", "#sd_card=1",
                                           "1|get.1 system.sd", "1|get.1 system.sd.files", "1|get.1 system.sd.x"])
        ans = [l for l in out.splitlines() if "system.sd" in l]
        self.assertIn("system.sd=[0,0,0,0]", ans[0])
        self.assertIn("system.sd.files=[]", ans[1])
        self.assertRegex(ans[2], r"system\.sd=\[1,1,1,[78]\]")              # 8 MB image: FAT12
        self.assertIn('system.sd.files=["PRESET_1.CFG:8","NOTES.TXT:1234"]', ans[3])
        self.assertIn("unknown command", ans[4])

    def test_export_round_trip(self):
        """set system.sd.export=N writes the settings as preset_N.cfg; the loader reads it back"""
        f = "V40-96ELN"
        out, ee, o = self.run_sd(f, [], ["#sd_card=1", "#sd_init", "1|set.1 gain.out.1=111", "1|set.1 gain.out.2=122",
                                         "1|set.1 biquad.1.2.calc=[1,1000,60,1414,0,128]",
                                         "1|set.1 system.sd.export=2", "#sd_cat=preset_2.cfg", "#sd_load",
                                         "1|set.1 system.sd.export=0", "1|set.1 system.sd.x=1"],
                                 at24=b"\xff" * 65536)
        self.assertIn("system.sd.export ok", out)
        cat = [l[8:] for l in out.splitlines() if l.startswith("@sd_cat:")]
        self.assertEqual(cat[1], "[96ELN]")
        self.assertIn('FILE_v10 = "gain.out.1=111"', cat)
        self.assertTrue(any(l.startswith('FILE_v10 = "mixer.position=') for l in cat))
        self.assertIn('FILE_v10 = "biquad.1.2.calc=[1,1000,60,1414,0,128]"', cat)
        self.assertFalse([l for l in cat if "[255," in l or '""' in l])          # unwritten storage left out
        self.assertFalse([l for l in cat if "<" in l or "!" in l or "#" in l[1:]])
        self.assertNotIn("gain.out.1 ok", out.split("system.sd.export ok")[1])   # own file: not loaded back
        self.assertIn("system.sd.export command out of range", out)
        self.assertIn("system.sd.x unknown command", out)
        # the exported file loads on another device
        text = "\n".join(cat) + "\n"
        out, ee, o = self.run_sd(f, [("preset_1.cfg", text, (2026, 11, 13, 12, 0))], ["#sd_card=1", "#sd_init", "#sd_load"],
                                 at24=b"\xff" * 65536)
        self.assertIn("*.1 gain.out.1 ok", out)
        self.assertIn("biquad.1.2.calc ok", out)
        # only mixer.position 0 of the empty harness state; delay.in.7/8 (digital inputs, no delay)
        # are not exported
        errs = sorted(set(re.findall(r"!\.1 (\S+) ", out)))
        self.assertEqual(errs, ["mixer.position"])
        self.assertFalse([l for l in cat if "delay.in.7" in l or "delay.in.8" in l])

    def test_backup(self):
        """#61: system.sd.backup.* copies flash, MCU EEPROM, preset EEPROM and DSP EEPROMs to the card"""
        f = "V40-96ELN"
        at24 = bytes((i * 7 + 3) & 0xFF for i in range(65536))
        steps = ["1|get.1 system.sd.backup", "1|set.1 system.sd.backup.eeprom=1",   # no card yet
                 "#sd_card=1", "#sd_init"] + [f"1|set.1 system.sd.backup.{p}=2" for p in ("flash", "eeprom", "preset", "1", "2", "3")] + [
                 "1|get.1 system.sd.files", '1|set.1 system.sd.open="PRESET_2.BIN"', "1|get.1 system.sd.read",
                 "1|set.1 system.sd.close=1", '1|set.1 system.sd.open="FLASH_2.BIN"', "1|get.1 system.sd.read",
                 "1|set.1 system.sd.close=1", "1|set.1 system.sd.backup.flash=0", "1|set.1 system.sd.backup.4=1",
                 "1|set.1 system.sd.backup.x=1"]
        out, _, _ = self.run_sd(f, [], steps, at24=at24)
        ans = [m.strip() for m in re.findall(r"^@0: (.*?)\\r\\n$", out, re.M)]
        self.assertEqual(ans[0], "<.1 system.sd.backup=[262144,4096,65536,3]")
        self.assertIn("!.1 system.sd.backup.eeprom", ans[1])                      # no card
        self.assertEqual(ans[2:8], [f"*.1 system.sd.backup.{p} ok" for p in ("flash", "eeprom", "preset", "1", "2", "3")])
        for name, size in (("FLASH_2.BIN", 262144), ("EEPROM_2.BIN", 4096), ("PRESET_2.BIN", 65536),
                           ("DSP1_2.BIN", 65536), ("DSP2_2.BIN", 65536), ("DSP3_2.BIN", 65536)):
            self.assertIn(f'"{name}:{size}"', ans[8])
        blk = lambda a: bytes.fromhex(a.split(',"')[1].split('"')[0])
        self.assertEqual(blk(ans[10]), at24[:128])                                 # the preset EEPROM itself
        self.assertEqual(blk(ans[13]), bytes((a ^ (a >> 8) ^ 0x5A) & 0xFF for a in range(128)))   # host flash
        self.assertIn("system.sd.backup.flash command out of range", ans[15])      # slot 1..9
        self.assertIn("!.1 system.sd.backup", ans[16])                              # no core 4
        self.assertIn("system.sd.backup.x unknown command", ans[17])
        # V30-96LN: two DSP cores
        out, _, _ = self.run_sd("V30-96LN", [], ["#sd_card=1", "#sd_init", "1|get.1 system.sd.backup",
                                                 "1|set.1 system.sd.backup.3=1"], at24=at24)
        self.assertIn("system.sd.backup=[262144,4096,65536,2]", out)
        self.assertIn("system.sd.backup.3 command out of range", out)

    def test_restore(self):
        """#65: system.sd.restore.* writes a backup back, checked by INFO_S.TXT, size and read-back,
        then restarts (the harness ends there)"""
        f = "V40-96ELN"
        at24 = bytes((i * 13 + 7) & 0xFF for i in range(65536))
        ans_of = lambda out: [m.strip() for m in re.findall(r"^@0: (.*?)\\r\\n", out, re.M)]
        out, _, o = self.run_sd(f, [], [
            "#sd_card=1", "#sd_init", "1|set.1 system.sd.backup.preset=2", "#sd_cat=INFO_2.TXT",
            "1|set.1 preset.erase=1",                                         # the preset EEPROM changes
            "1|set.1 system.sd.restore.preset=5",                             # no INFO_5.TXT
            "1|set.1 system.sd.restore.flash=2",                              # the flash: not from here
            "1|set.1 system.sd.restore.preset=2"], at24=at24)
        ans = ans_of(out)
        self.assertIn("@sd_cat:96ELN-4.0", out)                               # INFO: model-PCB
        self.assertIn("system.sd.restore.preset unknown para", " ".join(ans))
        self.assertIn("system.sd.restore.flash unknown command", " ".join(ans))
        self.assertEqual(ans[-1], "*.1 system.sd.restore.preset ok")
        self.assertEqual(o, at24)                                             # the erased preset is back
        out, ee, _ = self.run_sd(f, [], [
            "#sd_card=1", "#sd_init", "1|set.1 system.sd.backup.eeprom=3",
            '1|set.1 system.channel.name.1="CHANGED"', "1|set.1 system.sd.restore.eeprom=3"], at24=at24)
        self.assertEqual(ans_of(out)[-1], "*.1 system.sd.restore.eeprom ok")
        self.assertNotIn(b"CHANGED", ee)                                      # the MCU EEPROM as before

    def test_file_transfer(self):
        """#59: files to and from the card over the command line, in blocks with CRC-16/XMODEM"""
        def crc16(b):
            c = 0
            for x in b:
                c ^= x << 8
                for _ in range(8):
                    c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
            return c
        f = "V40-96ELN"
        data = bytes(range(256)) + b"forge" * 20                                    # 356 bytes = 3 blocks
        wr = lambda b: f'1|set.1 system.sd.write="{b.hex()}:{crc16(b):04X}"'
        writes = [wr(data[i:i + 40]) for i in range(0, len(data), 40)]
        steps = (["#sd_card=1", "#sd_init", '1|set.1 system.sd.create="LOG.BIN"'] + writes
                 + ["1|set.1 system.sd.close=1", "1|get.1 system.sd.files", '1|set.1 system.sd.open="log.bin"']
                 + ["1|get.1 system.sd.read"] * 4 + ["1|set.1 system.sd.seek=1", "1|get.1 system.sd.read",
                    '1|set.1 system.sd.delete="LOG.BIN"', "1|set.1 system.sd.close=1",
                    '1|set.1 system.sd.delete="LOG.BIN"', "1|get.1 system.sd.files",
                    '1|set.1 system.sd.open="../X"', '1|set.1 system.sd.write="00:0000"', "1|get.1 system.sd.read"])
        out, _, _ = self.run_sd(f, [], steps)
        ans = [l.split(": ", 1)[1].replace("\\r\\n", "").strip() for l in out.splitlines() if "system.sd" in l]
        self.assertEqual(ans[:2 + len(writes)], ["*.1 system.sd.create ok"] + ["*.1 system.sd.write ok"] * len(writes)
                         + ["*.1 system.sd.close ok"])
        self.assertIn('"LOG.BIN:356"', ans[2 + len(writes)])
        reads = [re.search(r"read=\[(\d+),\"([0-9A-F]*)\",(\d+)\]", a) for a in ans if "read=" in a]
        blocks = [(int(m[1]), bytes.fromhex(m[2]), int(m[3])) for m in reads]
        self.assertEqual([b[0] for b in blocks], [0, 1, 2, 3, 1])                  # 3 blocks, end, seek back
        self.assertEqual(b"".join(b[1] for b in blocks[:4]), data)
        for n, b, c in blocks:
            self.assertEqual(c, crc16(b) if b else 0)
        self.assertEqual(blocks[4][1], data[128:256])
        rest = ans[-7:]
        self.assertIn("!.1 system.sd.delete", rest[0])                             # still open
        self.assertIn("*.1 system.sd.delete ok", rest[2])
        self.assertIn("system.sd.files=[]", rest[3])
        self.assertIn("!.1 system.sd.open", rest[4])                               # no paths
        self.assertIn("!.1 system.sd.write", rest[5])                              # nothing open
        self.assertIn("!.1 system.sd.read", rest[6])                               # nothing open
        # a block without, with a wrong or with a cut crc is refused and not written
        bad = ['1|set.1 system.sd.create="B.BIN"', '1|set.1 system.sd.write="0102"', '1|set.1 system.sd.write="0102:1234"',
               f'1|set.1 system.sd.write="{bytes(48).hex()}:{crc16(bytes(48)):04X}"', wr(b"\x01\x02"),
               "1|set.1 system.sd.close=1", "1|get.1 system.sd.files"]
        out, _, _ = self.run_sd(f, [], ["#sd_card=1", "#sd_init"] + bad)
        self.assertEqual(out.count("!.1 system.sd.write"), 2)
        self.assertIn('"B.BIN:50"', out)                                          # 48 + 2 bytes

    def test_upload_of_a_preset_loads_at_the_next_start(self):
        def crc(b):
            c = 0
            for x in b:
                c ^= x << 8
                for _ in range(8):
                    c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
            return c
        f = "V40-96ELN"
        cfg = b'[96ELN]\nFILE_v10 = "gain.out.1=77"\n'
        steps = ["#sd_card=1", "#sd_init", "1|set.1 system.sd.export=1", "#sd_load",
                 '1|set.1 system.sd.create="PRESET_1.CFG"', f'1|set.1 system.sd.write="{cfg.hex()}:{crc(cfg):04X}"',
                 "1|set.1 system.sd.close=1", "#sd_load"]
        out, _, _ = self.run_sd(f, [], steps, at24=b"\xff" * 65536)
        after = out.split("system.sd.close ok")[1]
        self.assertIn("*.1 gain.out.1 ok", after)                                  # same date as the export, still loaded

    def log_records(self, out, which="@sd_hex:"):
        import struct
        hexes = [l[len(which):] for l in out.splitlines() if l.startswith(which)]
        recs = []
        for h in hexes:
            b = bytes.fromhex(h)
            recs.append([struct.unpack("<HIBBHH", b[i:i + 12]) for i in range(0, len(b), 12)])
        return recs

    def test_log(self):
        """#60: binary log, switched by system.debug.out bit 2 and system.debug.mode (level)"""
        f = "V41-96ELN-3"
        steps = ["#sd_card=1", "#sd_init", "#log_event=1,1,1,0,0", "#log_loop", "#sd_hex=FORGE.LOG",   # off: nothing
                 "1|set.1 system.debug.out=4", "1|set.1 system.debug.mode=4",
                 "#ms=1500", "#log_event=4,4,1,2,0", "#log_event=5,3,2,1,0",                      # debug > info: not written
                 "#i2cdev=1,68", "#i2cnack=2,6A,9", "#dsp_core=3",                               # error counter 5 -> ERR event
                 "#log_loop", "#sd_hex=FORGE.LOG"]
        out, _, _ = self.run_sd(f, [], steps)
        recs = self.log_records(out)
        self.assertEqual(recs[0], [])
        r = recs[1]
        lvl_mod = [(x[2] >> 5, x[2] & 31, x[3]) for x in r]
        self.assertEqual(lvl_mod[0], (1, 6, 1))                       # format record first
        self.assertEqual((r[0][4], r[0][5]), (1, 12))                 # version 1, 12 bytes
        self.assertIn((4, 4, 1), lvl_mod)                             # preset loaded, info
        self.assertNotIn((5, 3, 2), lvl_mod)                          # debug level filtered
        err = [x for x in r if x[2] & 31 == 2]
        self.assertEqual([(x[3], x[4]) for x in err], [(5, 1)])       # error type 5, count 1
        self.assertGreaterEqual(r[1][1], 1500)                        # ms since start

    def test_log_rotation_and_queue(self):
        f = "V40-96ELN"
        many = [f"#log_event=4,4,1,{i},0" for i in range(20)]           # 20 > queue of 16
        steps = (["#sd_card=1", "#sd_init", "1|set.1 system.debug.out=4", "1|set.1 system.debug.mode=4"] + many
                 + ["#log_loop", "#sd_hex=FORGE.LOG"] + [f"#log_event=4,4,1,{100 + i},0" for i in range(16)]
                 + ["#log_loop", "#log_event=4,4,1,200,0", "#log_loop", "#sd_hex=FORGE.LOG", "#sd_hex=FORGE.OLD"])
        out, _, _ = self.run_sd(f, [], steps)
        first, log, old = self.log_records(out)
        self.assertEqual([x[4] for x in first if x[3] == 1 and x[2] & 31 == 4], list(range(16)))
        self.assertEqual([(x[3], x[4]) for x in first if x[2] & 31 == 6][-1], (2, 4))   # 4 dropped
        # 240-byte limit in the native build: the full file became FORGE.OLD, the new one starts with the format record
        self.assertTrue(old)
        self.assertEqual((log[0][2] & 31, log[0][3]), (6, 1))
        self.assertEqual(log[-1][4], 200)

    def test_other_model_and_no_card(self):
        f = "V40-96ELN"
        cfg = '[96LN]\nFILE_v10 = "gain.out.1=111"\n'
        out, ee, o = self.run_sd(f, [("preset_1.cfg", cfg, None)], ["#sd_card=1", "#sd_init", "#sd_load"])
        self.assertNotIn("gain.out.1 ok", out)                          # section of another model
        self.assertNotIn("preset.erase", out)
        out, ee, o = self.run_sd(f, [("preset_1.cfg", cfg, None)], ["#sd_card=0", "#sd_init", "#sd_load"])
        self.assertIn("@sd_ready:0", out)

    def test_changed_date_reloads(self):
        f = "V40-96ELN"
        cfg = '[96ELN]\nFILE_v10 = "gain.out.1=50"\n'
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            d = pathlib.Path(d)
            (d / "p.cfg").write_text(cfg)
            lines = [f"#sd_mkfs={d / 'sd.img'},8", f"#sd_put=preset_1.cfg,{d / 'p.cfg'},2026,1,1,10,0",
                     "#sd_card=1", "#sd_init", "#sd_load", f"#sd_put=preset_1.cfg,{d / 'p.cfg'},2026,1,2,10,0",
                     "#sd_init", "#sd_load"]
            p, ee = run(f, lines, want_eeprom=True)
        self.assertEqual(p.stdout.count("gain.out.1 ok"), 2)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class StartupTest(LinesMixin, unittest.TestCase):
    """#40: system.startup.N stores start commands, they run after the first preset load."""

    def test_store_list_run(self):
        f = "V40-96ELN"
        p, ee = run(f, ['1|set.1 system.startup.1="set.1 gain.out.2:150"', '1|set.1 system.startup.3="set.1 mute.out.1:1"',
                        '1|set.1 system.startup.2="set.1 system.boot:1"', '1|set.1 system.startup.9="x"',
                        '1|set.1 system.startup.4="' + "x" * 33 + '"', "1|get.1 system.startup.*"],
                    eeprom=image(f), want_eeprom=True)
        out = [l for l in p.stdout.splitlines() if l.startswith("@0")]
        self.assertIn("system.startup.1 ok", out[0])
        self.assertIn("system.startup.3 ok", out[1])
        self.assertTrue(all("command out of range" in x for x in out[2:5]), out[2:5])
        self.assertIn('<.1 system.startup.1="set.1 gain.out.2:150"\\r\\n <.1 system.startup.2=""', out[5])
        self.assertIn('<.1 system.startup.3="set.1 mute.out.1:1"', out[5])
        self.assertEqual(len(out), 6)
        # next start: nothing before the preset load, then one command per pass, only once
        p2 = run(f, ["#sys_init_init", "#sys_init_loop", "1|get.1 gain.out.2", "#sys_init_loaded", "#sys_init_loop",
                     "1|get.1 gain.out.2", "1|get.1 mute.out.1", "#sys_init_loop", "1|get.1 mute.out.1",
                     "#sys_init_loaded", "#sys_init_loop", "#sys_init_loop"], eeprom=ee)
        b = ["".join(x) for x in p2]
        self.assertIn("gain.out.2=0", b[2])              # nothing before the preset load
        self.assertIn("gain.out.2 ok", b[4])             # answer of the start command
        self.assertIn("gain.out.2=150", b[5])
        self.assertIn("mute.out.1=0", b[6])              # one command per pass
        self.assertIn("mute.out.1 ok", b[7])
        self.assertIn("mute.out.1=1", b[8])
        self.assertEqual(b[9:12], ["", "", ""])           # second preset load: not again

    def test_time(self):
        """#67: ms of each start phase, -1 = not reached"""
        f = "V40-96ELN"
        out = self.lines(f, ["1|get.1 system.startup.time", "#ms=1200", "#store_init", "#ms=300", "#dac_init",
                             "1|get.1 system.startup.time", "#ms=100", "#store_init", "1|get.1 system.startup.time"])
        out = [l for l in out if "startup.time=" in l]
        self.assertIn("system.startup.time=[" + ",".join(["-1"] * 20) + "]", out[0])
        want = ["-1"] * 20
        want[2], want[4] = "1200", "1500"                 # BT_STORE (3), BT_DAC (5)
        self.assertIn("system.startup.time=[" + ",".join(want) + "]", out[-2])
        self.assertEqual(out[-1], out[-2])                # the first time counts

    def test_delete(self):
        f = "V40-96ELN"
        out = self.lines(f, ['1|set.1 system.startup.1="set.1 gain.out.2:150"', '1|set.1 system.startup.1=""',
                             "1|get.1 system.startup.1"])
        self.assertIn('<.1 system.startup.1=""', out[2])


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class IdentityTest(LinesMixin, unittest.TestCase):
    """#31: one build per group of identical variants, identity in the EEPROM."""

    def test_groups(self):
        lists = {"V40-96ELN": '["96ELN-4.0","CORED8-4.0","CORED8-4.1"]', "V40-96ELN-3": '["96ELN3-4.0","96ELN3-4.1"]',
                 "V30-96ELN": '["96ELN-4.0"]', "V30-96LN": '["96LN-3.0"]'}
        for f, l in lists.items():
            out = self.lines(f, ["1|get.1 system.key.info.list"])
            self.assertEqual(out[0], f"@0: <.1 system.key.info.list={l}\\r\\n", f)

    def test_set_identity_is_kept(self):
        f = "V40-96ELN"
        p, ee = run(f, ['1|set.1 system.key.info="CORED8-4.1"', "1|get.1 system.key.info", "1|get.1 system.info",
                        "1|get.1 system.key.info.type", "1|get.1 system.key.info.board",
                        '1|set.1 system.key.info="96ELN3-4.0"'], eeprom=image(f), want_eeprom=True)
        out = [l for l in p.stdout.splitlines() if l.startswith("@0")]
        self.assertIn("*.1 system.key.info ok", out[0])
        self.assertIn('="BNS-CORED8-4.1-', out[1])
        self.assertIn('=["CORED8",96000', out[2])
        self.assertIn('="CORED8"', out[3])
        self.assertIn('="4.1"', out[4])
        self.assertIn("command out of range", out[5])   # identity of another group
        p2 = run(f, ["1|get.1 system.key.info"], eeprom=ee)
        self.assertIn('="BNS-CORED8-4.1-', p2[0][0])     # kept in the EEPROM
        self.assertEqual(ee[0xF00:0xF02], bytes([0xA5, 2]))

    def test_unset_identity_is_the_flavour(self):
        for f, ident in (("V40-CORED8", "CORED8-4.0"), ("V41-96ELN-3", "96ELN3-4.1"), ("V40-96ELN", "96ELN-4.0")):
            out = self.lines(f, ["1|get.1 system.key.info"])
            self.assertIn(f"-{ident}-", out[0], f)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class LoudnessTest(LinesMixin, unittest.TestCase):
    """M6 (#6): MODC type 0x6C loudness, field layout of the original GUI (CTRL_LOUDNESS_Settings)."""

    PRE = ["#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#dsp_detect", "#pin=PIN_CPU_POWER_CTRL_IN,1",
           "#events_init", "#modc_init"]

    def blocks(self, f, ch, extra):
        offs = table_words(f, "data_flow_ch_bq_offsets")
        lim = table_bytes(f, "data_flow_ch_bq_limits")
        core = table_bytes(f, "data_flow_dsp_bq_core")
        reg = table_bytes(f, "data_flow_dsp_bq_reg")
        adr = {1: (1, 0x68), 2: (2, 0x68), 3: (2, 0x6A)}
        out = []
        for last in (1, 0):   # bass shelf = last-1, treble shelf = last
            nr = offs[ch] + lim[ch] - last
            bus, a = adr[core[nr]]
            out.append(f"#block={bus},{a:02X},{reg[nr]:02X},20")
        return out

    def ref(self, f, t, freq, tenths):
        if tenths <= 0:
            return "0080000000000000000000000000000000000000"
        raw = bq_reference(f, t, freq, tenths, 707.1, 0, 0x80)
        return b"".join(v.to_bytes(4, "big") for v in reversed(raw)).hex().upper()

    def test_boost_ramp_disable(self):
        for f in ("V40-96ELN", "V40-96ELN-3", "V30-96LN"):
            cfg = "1|set.1 modc.9=[1,108,10DF280F,70,0,0,0,00000000]"   # input 35/36, ref 223, 0.40, 0.15
            reads = self.blocks(f, 35, None) + self.blocks(f, 36, None)
            out = self.lines(f, self.PRE + [cfg, "1|set.1 event.70.value=183", "#modc"] + reads   # -20 dB
                             + ["1|set.1 event.70.value=193", "#modc"] + reads                      # -15 dB: ramp
                             + ["1|set.1 modc.9.enable=0", "#modc"] + reads)
            b = [l[7:] for l in out if l.startswith("@block:")]
            bass, treb = self.ref(f, 14, 100, 80), self.ref(f, 15, 10000, 30)
            self.assertEqual(b[0:4], [bass, treb, bass, treb], f)        # first write: target directly
            self.assertEqual(b[4], self.ref(f, 14, 100, 78), f)          # then 0.2 dB per pass
            self.assertEqual(b[5], self.ref(f, 15, 10000, 28), f)
            flat = self.ref(f, 0, 0, 0)
            self.assertEqual(b[8:12], [flat] * 4, f)                     # off: 0 dB at once

    def test_step_event2(self):
        """#51: event 2 = step per pass in 0.01 dB; 0.05 dB steps between the 0.1 dB table values."""
        f = "V40-96ELN"
        cfg = "1|set.1 modc.9=[1,108,10DF280F,70,5,0,0,00000000]"   # step 0.05 dB
        reads = self.blocks(f, 35, None)
        out = self.lines(f, self.PRE + [cfg, "1|set.1 event.70.value=183", "#modc"] + reads   # -20 dB
                         + ["1|set.1 event.70.value=193", "#modc"] + reads                      # -15 dB
                         + ["#modc"] + reads)
        b = [l[7:] for l in out if l.startswith("@block:")]
        self.assertEqual(b[0], self.ref(f, 14, 100, 80))      # first write: target (8.0 dB)
        self.assertEqual(b[2], self.ref(f, 14, 100, 79.5))    # then 0.05 dB per pass
        self.assertEqual(b[3], self.ref(f, 15, 10000, 29.5))
        self.assertEqual(b[4], self.ref(f, 14, 100, 79))      # 7.9 dB: table value again
        self.assertNotEqual(b[2], b[4])

    def test_follows_gain_spdif(self):
        f = "V40-96ELN"
        reads = self.blocks(f, 35, None)
        out = self.lines(f, self.PRE + ["1|set.1 gain.spdif.1=223",                          # 0 dB: no boost
                                        "1|set.1 modc.9=[1,108,10000000,0,0,0,0,00000000]",  # input path, defaults
                                        "#modc", "1|set.1 gain.spdif.1=203", "#modc"] + reads
                                       + ["#modc"] * 25 + reads)
        b = [l[7:] for l in out if l.startswith("@block:")]
        self.assertEqual(b[0], self.ref(f, 14, 100, 2))    # gain.spdif triggers the block: first 0.2 dB step
        self.assertEqual(b[1], self.ref(f, 15, 10000, 2))
        self.assertEqual(b[2], self.ref(f, 14, 100, 40))   # end: -10 dB below 223, 40 % = 4.0 dB
        self.assertEqual(b[3], self.ref(f, 15, 10000, 15))  # 15 % = 1.5 dB

    def test_type_version_and_cap(self):
        f = "V40-96ELN"
        out = self.lines(f, ["1|get.1 modc.version.attributes.ac", "1|set.1 modc.9.type=108", "1|get.1 modc.9.type"])
        self.assertIn("=[", out[0])
        self.assertEqual(out[0].split("[")[1].split(",")[12], "20")      # 0x6C & 0x3F = 44 = ac + 12
        self.assertIn("<.1 modc.9.type=108", out[2])
        reads = self.blocks(f, 35, None)
        out = self.lines(f, self.PRE + ["1|set.1 modc.9=[1,108,10006400,70,0,0,0,00000000]",  # slope 1.00
                                        "1|set.1 event.70.value=100", "#modc"] + reads)
        b = [l[7:] for l in out if l.startswith("@block:")]
        self.assertEqual(b[0], self.ref(f, 14, 100, 120))                # capped at 12 dB
        self.assertEqual(b[1], self.ref(f, 15, 10000, 92))               # flags 4 = 0: default 0.15 x 61.5 dB

    def test_output_pairs_frequency_and_mask_change(self):
        """GUI layout: flags 1 bits 0..3 = output pairs, sets 1+2 / 3+4 = shelf frequencies."""
        f = "V40-96ELN"
        r12 = self.blocks(f, 1, None) + self.blocks(f, 2, None)
        r34 = self.blocks(f, 3, None) + self.blocks(f, 4, None)
        cfg = "1|set.1 modc.9=[1,108,01DF280F,70,0,0,0,5000A00F]"     # pair 1, bass 80 Hz, treble 4000 Hz
        out = self.lines(f, self.PRE + [cfg, "1|set.1 event.70.value=183", "#modc"] + r12 + r34
                         + ["1|set.1 modc.9.flags=02DF280F", "#modc"] + r12 + r34)
        b = [l[7:] for l in out if l.startswith("@block:")]
        bass, treb, flat = self.ref(f, 14, 80, 80), self.ref(f, 15, 4000, 30), self.ref(f, 0, 0, 0)
        self.assertEqual(b[0:4], [bass, treb, bass, treb])                # outputs 1 and 2
        self.assertNotIn(bass, b[4:8])                                    # outputs 3/4 untouched
        self.assertEqual(b[8:12], [flat] * 4)                             # pair 1 dropped: flat again
        self.assertEqual(b[12:16], [bass, treb, bass, treb])              # pair 2 now


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class LevelTest(LinesMixin, unittest.TestCase):
    """#51: measured input level (PEEK) as volume source of the loudness block."""

    PRE = LoudnessTest.PRE

    def level(self, out):
        v = [l for l in out if l.startswith("@level:")][-1][7:].split(",")
        return int(v[0]), int(v[1])

    def test_scale_and_gate(self):
        f = "V40-96ELN"
        out = self.lines(f, ["#level_feed=-20,3000", "#level?", "#level_feed=-80,3000", "#level?",
                             "#level_feed=-30,3000", "#level?"])
        lv = [l for l in out if l.startswith("@level:")]
        self.assertEqual(lv[0], "@level:1,215")                    # -20 dBFS RMS = 255 - 40
        v = int(lv[1].split(",")[1])
        self.assertLessEqual(abs(v - 215), 2, lv[1])               # pause (-80 dBFS): held
        v = int(lv[2].split(",")[1])
        self.assertLessEqual(abs(v - 195), 2, lv[2])               # -30 dBFS: 195

    def test_short_impulse_ignored(self):
        """A short loud impulse (20 dB above the programme) does not move the level."""
        f = "V40-96ELN"
        out = self.lines(f, ["#level_feed=-30,1920", "#level?",          # 30 s at -30 dBFS
                             "#level_feed=-10,64", "#level?",            # 1 s block 20 dB louder
                             "#level_feed=-30,640", "#level?"])          # 10 s at -30 dBFS again
        lv = [int(l.split(",")[1]) for l in out if l.startswith("@level:")]
        self.assertEqual(lv[0], 195)
        self.assertEqual(lv[1], 195)                                     # median of 5 blocks
        self.assertEqual(lv[2], 195)

    def test_hysteresis(self):
        """Changes up to 1 dB do not move the reported level, larger ones do."""
        f = "V40-96ELN"
        out = self.lines(f, ["#level_feed=-30,1920", "#level?", "#level_feed=-30.8,3200", "#level?",
                             "#level_feed=-33,3200", "#level?"])
        lv = [int(l.split(",")[1]) for l in out if l.startswith("@level:")]
        self.assertEqual(lv[0:2], [195, 195])                            # -0.8 dB: held
        self.assertLessEqual(abs(lv[2] - 189), 2)                        # -3 dB: follows

    def test_loudness_follows_level(self):
        f = "V40-96ELN"
        reads = LoudnessTest.blocks(self, f, 35, None)
        cfg = "1|set.1 modc.9=[1,108,10D7280F,0,0,192,0,00000000]"   # path 35/36, ref 215, in7+in8
        out = self.lines(f, self.PRE + [cfg, "#modc"] + reads                  # no level yet: no boost
                         + ["#level_feed=-30,3000"] + ["#modc"] * 25 + reads)  # -10 dB below ref, ramp done
        b = [l[7:] for l in out if l.startswith("@block:")]
        flat = LoudnessTest.ref(self, f, 0, 0, 0)
        self.assertEqual(b[0:2], [flat, flat])
        self.assertEqual(b[2], LoudnessTest.ref(self, f, 14, 100, 40))       # 0.40 x 10 dB
        self.assertEqual(b[3], LoudnessTest.ref(self, f, 15, 10000, 15))

    def test_peek_on_the_bus(self):
        f = "V40-96ELN"
        cfg = "1|set.1 modc.9=[1,108,10D7280F,0,0,64,0,00000000]"    # in7 only
        out = self.lines(f, self.PRE + [cfg, "#modc", "#i2clog=1", "#level_loop", "#i2clog=0"])
        bus1 = " ".join(l for l in out if l.startswith("@i2c1:"))
        self.assertIn("S 68 06 00 02 00 06 P", bus1)               # PEEK data memory D_006 (in7)
        self.assertIn("S 68 07 Sr 69", bus1)                       # then read 8 bytes of 0x07


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class LimitsTest(LinesMixin, unittest.TestCase):
    """#54: system.limits, the limits of this build for a GUI."""

    def vals(self, line):
        return [int(x) for x in line.split("[")[1].split("]")[0].split(",") if x]

    def test_limits(self):
        for f in FLAVOURS:
            out = self.lines(f, ["1|get.1 system.limits", "1|get.1 system.limits.biquad", "1|get.1 system.limits.types",
                                 "1|get.1 system.limits.out", "1|get.1 system.limits.in", "1|get.1 system.limits.names",
                                 "1|get.1 system.limits.xx"])
            self.assertEqual(out[0], '@0: <.1 system.limits=["biquad","types","out","in","names"]\\r\\n', f)
            mn, mx = table_words(f, "data_ranges_biquad_min_I"), table_words(f, "data_ranges_biquad_max_I")
            s16 = lambda w: w - 0x10000 if w & 0x8000 else w
            want = [v for c in range(2, 6) for v in (s16(mn[c]), s16(mx[c]))]
            self.assertTrue(out[1].startswith("@0: <.1 system.limits.biquad=["), f)
            self.assertEqual(self.vals(out[1]), want, f)
            self.assertEqual(self.vals(out[2]), list(range(23)), f)
            nout, samples = consts(f, "FLOW_MAX_OUT_CHANNEL", "FLOW_MAX_SAMPLES")
            dd = table_bytes(f, "data_flow_pair_delay_dsp")
            self.assertEqual(self.vals(out[3]), [samples] + [dd[i] for i in range(1, nout + 1)], f)
            has_in, = consts(f, "FEATURE_INPUT_DELAY")
            if has_in:
                n_in, = consts(f, "STORE_DELAY_IN_COUNT")
                di = table_bytes(f, "data_flow_pair_delay_in_dsp")
                self.assertEqual(self.vals(out[4]), [samples] + [di[i] for i in range(1, n_in + 1)], f)
            else:
                self.assertEqual(out[4], "@0: <.1 system.limits.in=[]\\r\\n", f)
            self.assertEqual(self.vals(out[5]), [16, 16], f)
            self.assertIn("unknown command", out[6], f)
        # 96ELN: biquad ranges as the BASCOM tables (-12..+6 dB, Q 0.001..10, 10..22000 Hz)
        self.assertEqual(self.vals(self.lines("V40-96ELN", ["1|get.1 system.limits.biquad"])[0]), [10, 22000, -120, 60, 1, 10000, 0, 22000])


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class InputNameTest(unittest.TestCase):
    """#55: names of the inputs in the device, system.channel.in.N, outside the original layout."""

    def test_names(self):
        for f in ("V40-96ELN", "V30-96LN"):
            p, ee = run(f, ["1|get.1 system.channel.in.1", '1|set.1 system.channel.in.1="Radio links"',
                            '1|set.1 system.channel.in.8="Ein ganz langer Name"', "1|get.1 system.channel.in.*",
                            '1|set.1 system.channel.in.9="x"', "1|set.1 system.channel.in.2=5",
                            '1|set.1 system.channel.in.1=""', "1|get.1 system.channel.in.1", "1|get.1 system.channel.name.1"],
                        eeprom=image(f), want_eeprom=True)
            out = [l for l in p.stdout.splitlines() if l.startswith("@0:")]
            self.assertEqual(out[0], '@0: <.1 system.channel.in.1=""\\r\\n', f)             # never written (0xFF)
            self.assertIn("system.channel.in.1 ok", out[1], f)
            listing = out[3].split("\\r\\n")
            self.assertEqual(listing[0], '@0: <.1 system.channel.in.1="Radio links"', f)
            self.assertEqual(listing[7].strip(), '<.1 system.channel.in.8="Ein ganz langer "', f)  # 16 characters
            self.assertIn("command out of range", out[4], f)
            self.assertIn("unknown", out[5], f)                                                 # not a string
            self.assertEqual(out[7], '@0: <.1 system.channel.in.1=""\\r\\n', f)
            self.assertIn("system.channel.name.1=", out[8], f)                                  # output names unchanged
            self.assertEqual(ee[0x0C00 + 7 * 17:0x0C00 + 8 * 17], b"Ein ganz langer \x00")              # 16 + NUL at 0x0C77
            layout = layout_end(f)
            self.assertLess(layout, 0x0C00, f)                                                  # outside the original layout


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class VuTest(LinesMixin, unittest.TestCase):
    """#52: system.vu, RMS level of the mixer inputs for the level meter of the GUI."""

    PRE = ["#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#dsp_detect"]

    def vu(self, line):
        return [int(x) for x in line.split("[")[1].split("]")[0].split(",")]

    def test_scale_and_inputs(self):
        for f in ("V40-96ELN", "V30-96LN"):
            out = self.lines(f, self.PRE + ["1|get.1 system.vu",                    # arms the measurement
                                            "#vu_feed=7,-20,200", "#vu_feed=8,-26,200", "#vu_feed=1,-60,200",
                                            "1|get.1 system.vu", "1|get.1 system.vu.x"])
            self.assertEqual(self.vu(out[0]), [0] * 8, f)
            self.assertEqual(self.vu(out[1]), [135, 0, 0, 0, 0, 0, 215, 203], f)   # 255 + 2 dB
            self.assertIn("unknown command", out[2])

    def test_runs_only_while_asked(self):
        f = "V40-96ELN"
        loops = ["#level_loop"] * 4
        out = self.lines(f, self.PRE + ["#i2clog=1"] + loops + ["#vu?", "1|get.1 system.vu", "#vu?"] + loops
                         + ["#i2clog=0"] + ["#level_loop"] * 200 + ["#vu?"])
        peeks = [l for l in out if l.startswith("@i2c1:") and "S 68 06 00 02" in l]
        # nothing before the first request; then two inputs per call, each data address once per round
        self.assertEqual(len(peeks), 8, peeks)
        addr = [int(l.split("S 68 06 00 02 00 ")[1][:2], 16) for l in peeks]
        self.assertEqual(addr, [4, 5, 2, 3, 0, 1, 7, 6])                          # G1..G8 = in5, in6, in3, in4, in1, in2, in8, in7
        self.assertEqual([l for l in out if l.startswith("@vu:")], ["@vu:0,0", "@vu:1,0", "@vu:0,0"])   # off again after 3 s

    def test_outputs(self):
        """#52 step 2: system.vu.out = outputs 1..8 from the DO words of cores 3 (1-4) and 2 (5-8)."""
        f = "V40-96ELN"
        out = self.lines(f, self.PRE + ["1|get.1 system.vu.out", "#vo_feed=1,-20,200", "#vo_feed=8,-6,200",
                                        "1|get.1 system.vu.out", "#vu?"])
        self.assertEqual(self.vu(out[0]), [0] * 8)
        self.assertEqual(self.vu(out[1]), [215, 0, 0, 0, 0, 0, 0, 243])
        self.assertIn("@vu:0,1", out[2])                                           # inputs stay off
        loops = ["#level_loop"] * 4
        for f, words in (("V40-96ELN", ["1C0", "1C1", "1BE", "1BF"]), ("V40-96ELN-3", ["1C4", "1C5", "1C2", "1C3"])):
            out = self.lines(f, self.PRE + ["1|get.1 system.vu.out", "#i2clog=1"] + loops)
            peeks = [l for l in out if l.startswith("@i2c2:") and " 06 00 02 " in l]
            got = [(l.split("S ")[1][:2], l.split(" 06 00 02 ")[1][:5].replace(" ", "")) for l in peeks]
            want = [("6A", "0" + w) for w in words] + [("68", "0" + w) for w in words]
            self.assertEqual(got, want, f)                                         # outputs 1-4 core 3, 5-8 core 2
        out = self.lines("V30-96LN", self.PRE + ["1|get.1 system.vu.out"])
        self.assertIn("unknown command", out[0])                                   # no known output words

    def test_loudness_measurement_keeps_its_rate(self):
        """The level meter adds PEEKs, it does not take any from the loudness block (#51)."""
        f = "V40-96ELN"
        cfg = "1|set.1 modc.9=[1,108,10D7280F,0,0,64,0,00000000]"                 # loudness measures in7
        out = self.lines(f, LoudnessTest.PRE + [cfg, "#modc", "1|get.1 system.vu", "#i2clog=1"] + ["#level_loop"] * 8)
        peeks = [int(l.split("S 68 06 00 02 00 ")[1][:2], 16) for l in out if l.startswith("@i2c1:") and "S 68 06 00 02" in l]
        self.assertEqual(peeks.count(6), 8 + 2)                                    # 8 for loudness, 2 from the meter rounds
        self.assertEqual(len(peeks), 8 * 3)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class HelpTest(LinesMixin, unittest.TestCase):
    """get system.help lists every system command (port extension)."""

    def help(self, f="V40-96ELN"):
        line = self.lines(f, ["1|get.1 system.help"])[0]
        self.assertIn("system.help=[", line)
        return re.findall(r'"([^"]*)"', line.split("=", 1)[1])

    def test_matches_dispatchers(self):
        """help is built from the dispatch tables: every row listed, command words unique"""
        src = pathlib.Path(HERE, "..", "src", "cmd", "cmd_system.c").read_text()
        cmds = self.help()
        self.assertEqual(len(cmds), len(set(cmds)))
        for verb in ("get", "set"):
            table = src.split(f"#define SYS_{verb.upper()}(X) \\\n")[1].split("\n\n")[0]
            rows = re.findall(r'X\("([^"]*)", ', table)
            self.assertGreater(len(rows), 5)
            words = [r[:2] for r in rows]
            self.assertEqual(len(words), len(set(words)), f"{verb}: two rows with the same command word")
            for r in rows:
                name, *paths = r.split("|")
                want = [f"{verb} system.{name}{x}" for x in (paths or [""])]
                for w in want:
                    self.assertIn(w, cmds)
        self.assertIn("get system.vu.out", cmds)
        self.assertIn("set system.biquads.K", cmds)
        self.assertNotIn("set system.vu", cmds)

    def test_every_get_is_answered(self):
        f = "V40-96ELN"
        gets = [c[4:].replace(".N", ".1").replace(".K", ".1") for c in self.help() if c.startswith("get ")]
        out = self.lines(f, ["#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#dsp_detect"]
                         + [f"1|get.1 {c}" for c in gets])
        for c in gets:
            ans = [l for l in out if f" {c}" in l]
            self.assertTrue(ans, c)
            self.assertNotIn("unknown command", ans[0], c)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class LimiterCmdTest(LinesMixin, unittest.TestCase):
    """#58: system.dsp.limiter, coefficients over I2C to the limiter block of program 2"""
    PRE = ["#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#dsp_detect"]

    @staticmethod
    def w523(v):
        return round(v * (1 << 23)) & 0xFFFFFFFF

    def writes(self, out, dev, sub):
        """data words of the writes to a DSP subaddress, in address order (first on the bus first)"""
        got = []
        for l in out:
            if l.startswith("@i2c2:") and f"S {dev} {sub:02X} " in l and "Sr" not in l:
                b = [int(x, 16) for x in l.split(f"S {dev} {sub:02X} ")[1].split(" P")[0].split()]
                got.append([int.from_bytes(bytes(b[i:i + 4]), "big") for i in range(0, len(b), 4)])
        return got

    def test_threshold_attack_release(self):
        f = "V40-96ELN"
        out = self.lines(f, self.PRE + ["#i2clog=1", "1|set.1 system.dsp.limiter.1=-60", "#i2clog=0",
                                        "1|get.1 system.dsp.limiter", "1|get.1 system.dsp.limiter.1",
                                        "1|get.1 system.dsp.limiter.attack", "1|get.1 system.dsp.limiter.release",
                                        "1|set.1 system.dsp.limiter.1=-250", "1|set.1 system.dsp.limiter.9=-10",
                                        "1|set.1 system.dsp.limiter.release=0", "1|set.1 system.dsp.limiter.x=1"])
        text = "\n".join(out)
        t = 10 ** (-6.0 / 20)
        # output 1 = DO3 of core 3 (0x6A): subaddress 0xF2 = [1/T, T]
        self.assertIn([self.w523(1 / t), self.w523(t)], self.writes(out, "6A", 0xF2))
        self.assertIn([0x00800000, 0x00800000], self.writes(out, "68", 0xF2))      # output 5 off
        rel = 1 - math.exp(-1000 / (100 * 96000))
        att = math.exp(-10000 / (1 * 96000))
        shared3, shared2 = self.writes(out, "6A", 0xF4)[-1], self.writes(out, "68", 0xF4)[-1]
        self.assertAlmostEqual(shared3[0], self.w523(att), delta=1)                   # core 3 limits: attack (to523 truncates)
        self.assertEqual(shared2[0], 0x00800000)                                     # core 2 all off: transparent
        self.assertEqual(shared3[1] + shared3[2], 0x00800000)                        # 1 - rel + rel = 1 exactly
        self.assertAlmostEqual(shared3[2], self.w523(rel), delta=1)
        self.assertIn("system.dsp.limiter=[-60,0,0,0,0,0,0,0]", text)
        self.assertIn("system.dsp.limiter.1=-60", text)
        self.assertIn("system.dsp.limiter.attack=1", text)
        self.assertIn("system.dsp.limiter.release=100", text)
        self.assertEqual(text.count("command out of range"), 3)
        self.assertIn("system.dsp.limiter.x unknown command", text)

    def test_stored_and_not_on_v30_96ln(self):
        f = "V40-96ELN"
        p, ee = run(f, self.PRE + ["1|set.1 system.dsp.limiter.8=-125", "1|set.1 system.dsp.limiter.release=250"],
                    want_eeprom=True)
        p2, _ = run(f, self.PRE + ["1|get.1 system.dsp.limiter", "1|get.1 system.dsp.limiter.release"], eeprom=ee,
                    want_eeprom=True)
        self.assertIn("system.dsp.limiter=[0,0,0,0,0,0,0,-125]", p2.stdout)          # 0.5 dB steps
        self.assertIn("system.dsp.limiter.release=250", p2.stdout)
        out = self.lines("V30-96LN", ["1|get.1 system.dsp.limiter"])
        self.assertIn("unknown command", out[0])


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class BootEntryTest(unittest.TestCase):
    """system.boot writes the bootloader sequence 0xAA/0x55 to EEPROM 0/1 (entry without the key)."""

    def test_sequence(self):
        for f in FLAVOURS:
            p, ee = run(f, ["1|set.1 system.boot"], eeprom=image(f), want_eeprom=True)
            self.assertIn("system.boot ok", p.stdout, f)
            self.assertEqual(ee[0:2], b"\xAA\x55", f)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class BootSectionTest(LinesMixin, unittest.TestCase):
    """Diagnostics for #22: read-only dump of the boot section, 64 bytes per block."""

    def test_blocks(self):
        out = self.lines("V40-96ELN", ["1|get.1 system.hardware.boot.1", "1|get.1 system.hardware.boot.128",
                                       "1|get.1 system.hardware.boot.0", "1|get.1 system.hardware.boot.129"])
        self.assertRegex(out[0], r'<\.1 system\.hardware\.boot\.1="[0-9A-F]{128}"')
        self.assertIn("system.hardware.boot.128=", out[1])
        self.assertIn("command out of range", out[2])
        self.assertIn("command out of range", out[3])

    def test_dsp_load(self):
        """#45: wait for the TAS3108 program load per bus (host harness: no waits)."""
        out = self.lines("V40-96ELN", ["1|get.1 system.hardware.dsp"])
        self.assertIn("<.1 system.hardware.dsp=[0,0,0,0,0,0,0,0,0,0,0]", out[0])


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class SupplyTest(LinesMixin, unittest.TestCase):
    """#46: supply voltage monitoring (VCC internal, supply over a GVOL input with calibration)."""

    CAL = ["#adc_init", "1|set.1 system.supply.source=1", "1|set.1 system.supply.cal=[1000,10000,1400,14000]"]

    def test_vcc_and_unconfigured(self):
        out = self.lines("V40-96ELN", ["#adc=1,1,1556", "#supply_loop", "1|get.1 system.supply",
                                       "1|get.1 system.supply.source", "1|get.1 system.supply.low"])
        self.assertIn("system.supply=[3301,-1]", out[0])
        self.assertIn("system.supply.source=0", out[1])
        self.assertIn("system.supply.low=0", out[2])

    def test_vcc_calibration(self):
        """One-point VCC calibration: 3113 mV read, 3320 mV measured (DEV unit)."""
        out = self.lines("V40-96ELN", ["#adc=1,1,1479", "#supply_loop", "1|get.1 system.supply",
                                       "1|set.1 system.supply.vcc=3320", "1|get.1 system.supply.vcc",
                                       "1|get.1 system.supply", "1|set.1 system.supply.vcc=5000"])
        ans = [l for l in out if "@0:" in l]
        self.assertIn("system.supply=[3113,-1]", ans[0])
        self.assertIn("system.supply.vcc ok", ans[1])
        self.assertIn("system.supply.vcc=10664", ans[2])               # 3320/3113
        self.assertIn("system.supply=[3319,-1]", ans[3])
        self.assertIn("command out of range", ans[4])                  # > 20 % off

    def test_calibrated_supply(self):
        out = self.lines("V40-96ELN", self.CAL + ["#adc=0,0,1240", "#adc_loop", "#supply_loop",
                                                  "1|get.1 system.supply", "1|get.1 system.supply.cal",
                                                  "1|get.1 system.supply.raw"])
        ans = [l for l in out if "<.1" in l]
        self.assertRegex(ans[0], r"system\.supply=\[\d+,12400\]")
        self.assertIn("system.supply.cal=[1000,10000,1400,14000]", ans[1])
        self.assertIn(",1240,", ans[2])

    def test_invalid_values(self):
        out = self.lines("V40-96ELN", ["1|set.1 system.supply.source=3", "1|set.1 system.supply.cal=[5,1,5,2]",
                                       "1|set.1 system.supply.mute=2", "1|set.1 system.supply.cal=[1,2,3]"])
        for l in out:
            self.assertIn("command out of range", l)

    def test_low_event_and_mute(self):
        pre = self.CAL + ["1|set.1 system.supply.low=11000", "1|set.1 system.supply.mute=1"]
        low = ["#adc=0,0,1050", "#adc_loop"] + ["#supply_loop"] * 8   # 10.5 V
        mid = ["#adc=0,0,1120", "#adc_loop"] + ["#supply_loop"] * 8   # 11.2 V: inside the hysteresis
        ok = ["#adc=0,0,1140", "#adc_loop"] + ["#supply_loop"] * 8    # 11.4 V
        out = self.lines("V40-96ELN", pre + ["#adc=0,0,1240", "#adc_loop"] + ["#supply_loop"] * 8 + low
                         + ["#mutechg"] + mid + ok)
        ev = [l for l in out if l.startswith("@0:") and "#.1 system.supply=" in l]  # USB port
        self.assertEqual(len(ev), 2, out)
        self.assertIn("system.supply=low", ev[0])
        self.assertIn("system.supply=ok", ev[1])


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class MonoTest(LinesMixin, unittest.TestCase):
    """#14: MODC type 0x6A mono per output pair, on the input mixer rows (C 3..10 = outputs 1..8)."""

    PRE = ["#pin=PIN_CPU_POWER_CTRL_IN,1", "#events_init", "#dtr=1", "#modc_init", "1|set.1 mixer.position=1",
           "1|set.1 mixer.1.9.2=100", "1|set.1 mixer.1.10.1=80",      # out 7 <- Kanal 1 L, out 8 <- Kanal 1 R
           "1|set.1 mixer.1.3.2=100"]                                 # out 1 <- Kanal 1 L (not in the pair)
    VALS = ["#mixval=9,2", "#mixval=9,1", "#mixval=10,2", "#mixval=10,1", "#mixval=3,2"]

    def vals(self, out):
        return [int(l.split(":")[1]) for l in out if l.startswith("@mix:")]

    def test_modes(self):
        for f in ("V40-96ELN", "V30-96LN"):
            for mode, want in ((1, [100, 0, 0, 80, 100]), (2, [0, 80, 0, 80, 100]),
                               (3, [100, 0, 100, 0, 100]), (4, [50, 40, 50, 40, 100])):
                out = self.lines(f, self.PRE + [f"1|set.1 modc.9=[1,106,08000000,0,0,0,0,{mode:02X}000000]", "#modc"]
                                 + self.VALS)
                self.assertEqual(self.vals(out), want, (f, mode))

    def test_event_default_pair_and_tuning_kept(self):
        f = "V40-96ELN"
        out = self.lines(f, self.PRE + ["1|set.1 modc.9=[1,106,00000000,70,0,0,0,04000000]",   # flags 0: pair 7/8
                                        "#modc"] + self.VALS
                         + ["1|set.1 event.70.value=1", "#modc"] + self.VALS
                         + ["1|set.1 modc.9.enable=0", "#modc"] + self.VALS
                         + ["1|get.1 modc.version.attributes.ac"])
        v = self.vals(out)
        self.assertEqual(v[0:5], [100, 0, 0, 80, 100])     # event 70 = 0: off
        self.assertEqual(v[5:10], [50, 40, 50, 40, 100])   # event on: stereo to mono
        self.assertEqual(v[10:15], [100, 0, 0, 80, 100])   # block off: stereo again, tuning unchanged
        self.assertEqual(out[-1].split("[")[1].split(",")[10], "20")   # 0x6A & 0x3F = 0x2A = ac + 10


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class MixerModcTest(LinesMixin, unittest.TestCase):
    """#16: MODC type 0x6D mixer, sets 1..3 = gain of analog input pairs 1..3 (G 1/2, 3/4, 5/6)."""

    PRE = ["#pin=PIN_CPU_POWER_CTRL_IN,1", "#events_init", "#dtr=1", "#modc_init", "1|set.1 mixer.position=1",
           "1|set.1 mixer.1.3.2=100", "1|set.1 mixer.1.3.3=80",       # out 1 <- pair 1 L, pair 2 R
           "1|set.1 mixer.1.4.6=60", "1|set.1 mixer.1.4.7=100",       # out 2 <- pair 3 L, digital
           "1|set.1 mixer.1.1.1=100"]                                 # sum before chain 1 <- pair 1 R
    VALS = ["#mixval=3,2", "#mixval=3,3", "#mixval=4,6", "#mixval=4,7", "#mixval=1,1"]

    def vals(self, out):
        return [int(l.split(":")[1]) for l in out if l.startswith("@mix:")]

    def test_gains_and_off(self):
        for f in ("V40-96ELN", "V30-96LN"):
            out = self.lines(f, self.PRE + self.VALS
                             + ["1|set.1 modc.9=[1,109,00000000,0,0,0,0,32004BFF]", "#modc"] + self.VALS
                             + ["1|set.1 modc.9.enable=0", "#modc"] + self.VALS
                             + ["1|get.1 modc.version.attributes.ac"])
            v = self.vals(out)
            self.assertEqual(v[0:5], [100, 80, 60, 100, 100], f)
            self.assertEqual(v[5:10], [50, 0, 45, 100, 50], f)        # 50 %, 0 %, 75 %; digital untouched
            self.assertEqual(v[10:15], [100, 80, 60, 100, 100], f)    # block off: tuning unchanged
            self.assertEqual(out[-1].split("[")[1].split(",")[13], "20", f)   # 0x6D & 0x3F = 0x2D = ac + 13

    def test_with_mono(self):
        f = "V40-96ELN"
        out = self.lines(f, self.PRE + ["1|set.1 mixer.1.4.2=40",
                                        "1|set.1 modc.9=[1,106,01000000,0,0,0,0,04000000]",   # pair 1/2 sum
                                        "1|set.1 modc.10=[1,109,00000000,0,0,0,0,32646400]", "#modc",
                                        "#mixval=3,2", "#mixval=4,2"])
        self.assertEqual(self.vals(out), [35, 35])                     # (100+40)/2 = 70, then 50 %


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class MuxITest(LinesMixin, unittest.TestCase):
    """#17: MODC type 0x60 mux_I, priority list of events as the GUI dialog "Eingangs-Umschalter"."""

    PRE = ["#pin=PIN_CPU_POWER_CTRL_IN,1", "#events_init", "#dtr=1", "#modc_init", "1|set.1 mixer.position=1"]

    def test_priority_and_fallback(self):
        f = "V40-96ELN"
        cfg = "1|set.1 modc.9=[1,96,01000000,70,71,72,0,01020300]"   # position 1; fallback 1, ev71 -> 2, ev72 -> 3
        get = ["1|get.1 mixer.in"]
        out = self.lines(f, self.PRE + [cfg, "#modc"] + get
                         + ["1|set.1 event.71.value=1", "#modc"] + get
                         + ["1|set.1 event.72.value=1", "#modc"] + get           # higher priority wins
                         + ["1|set.1 event.72.value=0", "1|set.1 event.71.value=0", "#modc"] + get
                         + ["1|get.1 modc.version.attributes.ac"])
        got = [l.split("mixer.in=")[1].split("\\")[0] for l in out if "<.1 mixer.in=" in l]
        self.assertEqual(got, ["1", "2", "3", "1"])
        self.assertTrue(out[-1].split("[")[1].startswith("20,"))     # type 0x60 & 0x3F = 0x20 = first of "ac"

    def test_position_not_ticked(self):
        f = "V40-96ELN"
        out = self.lines(f, self.PRE + ["1|set.1 mixer.in=2", "1|set.1 modc.9=[1,96,04000000,70,71,0,0,01030000]",
                                        "1|set.1 event.71.value=1", "#modc", "1|get.1 mixer.in"])
        self.assertIn("mixer.in=2\\r", out[-1])                      # only position 2 ticked (bit 2)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class PresetCmdTest(unittest.TestCase):
    """cmd_preset: loaded (via the MODC cleanup), copy, erase, select."""

    def test_preset_commands(self):
        import random
        import tempfile
        for f in ("V41-96ELN-3", "V30-96LN"):
            rnd = random.Random(7)
            nmax, fs, cb = consts(f, "MODUL_MAX_PRESET", "STORAGE_FRAMESIZE", "STORAGE_CHKSUM_BASE")
            vin_b, vin_d, mux_b, mux_d = consts(f, "STORAGE_VOL_IN_BASE", "STORE_VOL_IN_DEFAULT",
                                                "STORAGE_MUX_BASE", "STORE_MUX_DEFAULT")
            cores, nbq = consts(f, "MODUL_TAS3108_CORES", "FLOW_MAX_DSP_BIQUADS")
            lim_v, lim_c, lim_bq, lim_tv = consts(f, "LIMIT_STORE_ADDR_VALUES", "LIMIT_STORE_ADDR_BQCLUSTER",
                                                  "LIMIT_STORE_ADDR_BQ", "LIMIT_STORE_ADDR_BQTV")
            av, ac = table_words(f, "data_store_addr_values"), table_words(f, "data_store_addr_bqcluster")
            abq, atv = table_words(f, "data_store_addr_bq"), table_words(f, "data_store_addr_bqtv")
            at24 = bytearray(rnd.randrange(256) for _ in range(65536))
            dspee = {n: bytearray(rnd.randrange(256) for _ in range(65536)) for n in (1, 2, 3)}
            frame = bytearray(rnd.randrange(256) for _ in range(fs))
            frame[0] = 1
            frame[mux_b - 1:mux_b + 3] = bytes([1, 1, 1, 1])
            frame[cb - 1:cb + 1] = xmodem(frame[:cb - 1]).to_bytes(2, "little")
            at24[av[0]:av[0] + fs] = frame
            for nr in range(1, nbq + 1):  # all biquads default: fast load
                at24[atv[0] + (nr + 1) * 10 - 1] = 0
            with tempfile.TemporaryDirectory() as d:
                d = pathlib.Path(d)
                (d / "at24.bin").write_bytes(bytes(at24))
                lines = [f"#at24={d / 'at24.bin'}", f"#at24out={d / 'at24o.bin'}", "#dtr=1", "#ir=1", "#store_init"]
                for n in (1, 2, 3):
                    (d / f"dsp{n}.bin").write_bytes(bytes(dspee[n]))
                    lines += [f"#dspee={n},{d / f'dsp{n}.bin'}", f"#dspee_out={n},{d / f'dspo{n}.bin'}"]
                lines += ["1|set.1 preset.copy=1", "1|set.1 preset.loaded=0", "#mainloop", "1|get.1 preset.loaded",
                          "1|set.1 preset.copy=2", "1|set.1 preset.erase=3", "1|set.1 preset.select=1",
                          "1|get.1 preset.select", f"1|set.1 preset.copy={nmax}", "1|set.1 preset.erase=-1"]
                p = subprocess.run([os.path.join(NATIVE, f"cmd-{f}")], input="\n".join(lines) + "\n",
                                   capture_output=True, text=True, timeout=30)
                self.assertEqual(p.returncode, 0, p.stderr)
                o = (d / "at24o.bin").read_bytes()
                eo = {n: (d / f"dspo{n}.bin").read_bytes() for n in (1, 2, 3)}
            out = [l.rstrip("-") for l in p.stdout.splitlines() if l.startswith("@0")]
            self.assertIn("command out of range", out[0], f)          # copy before a preset is loaded
            self.assertEqual(out[1], "@0: *.1 preset.loaded ok\\r\\n", f)
            self.assertIn("#.1 preset.loaded=0\\r\\n", p.stdout, f)   # event after loading
            self.assertIn(" <.1 preset.loaded=0\\r\\n", p.stdout, f)
            self.assertIn("@0: *.1 preset.copy ok\\r\\n", out, f)
            self.assertIn(" #.1 preset.erase=3\\r\\n", p.stdout, f)
            self.assertIn("@0: <.1 preset.select=1\\r\\n", out, f)
            self.assertIn("command out of range", out[-2], f)
            self.assertIn("command out of range", out[-1], f)
            # copy 0 -> 2: values, cluster, biquad types, DSP EEPROMs of every core
            for tab, lim in ((av, lim_v), (ac, lim_c), (atv, lim_tv)):
                self.assertEqual(o[tab[2]:tab[2] + lim], at24[tab[0]:tab[0] + lim], f)
            for n, (bus, dev) in enumerate([(1, 0xA0), (2, 0xA0), (2, 0xA2)][:cores], 1):
                self.assertEqual(eo[n][abq[2]:abq[2] + lim_bq], dspee[n][abq[0]:abq[0] + lim_bq], (f, n))
            # erase 3: defaults with checksum, all-pass types, default biquads with CRC
            e = o[av[3]:av[3] + fs]
            self.assertEqual(e[0], 1, f)
            self.assertEqual(e[vin_b - 1], vin_d, f)
            self.assertEqual(e[mux_b - 1], mux_d, f)
            self.assertEqual(int.from_bytes(e[cb - 1:cb + 1], "little"), xmodem(e[:cb - 1]), f)
            self.assertTrue(all(o[atv[3] + (nr + 1) * 10 - 1] == 0 for nr in range(1, nbq + 1)), f)
            core_t = table_bytes(f, "data_flow_dsp_bq_core")
            offs = table_words(f, "data_flow_bq_storage_offset")
            data = bytes.fromhex("0080000000000000000000000000000000000000")
            for nr in (1, nbq // 2, nbq):
                c = core_t[nr]
                a = abq[3] + (nr - offs[c]) * 22
                self.assertEqual(eo[c][a:a + 22], xmodem(data[::-1]).to_bytes(2, "big") + data, (f, nr))
            # preset 1 and the selected preset untouched by copy/erase
            self.assertEqual(o[av[1]:av[1] + lim_v], at24[av[1]:av[1] + lim_v], f)


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class PresetLoadTest(unittest.TestCase):
    """api_preset_load (modc_load_preset): biquads from the DSP EEPROMs, values frame from the
    24xx512, applied to DSPs and DAC; remote out stays off (FORGE_AUDIO=0)."""

    def test_unchanged_biquads_not_written_again(self):
        """#66: a preset change writes only the biquad places whose coefficients differ"""
        import random
        import tempfile
        f = "V41-96ELN-3"
        rnd = random.Random(7)
        fs, cb = consts(f, "STORAGE_FRAMESIZE", "STORAGE_CHKSUM_BASE")
        core_t = table_bytes(f, "data_flow_dsp_bq_core")
        offs = table_words(f, "data_flow_bq_storage_offset")
        at24 = bytearray(b"\xff" * 65536)
        dspee = {n: bytearray(b"\xff" * 65536) for n in (1, 2, 3)}
        bases, bqtvs = table_words(f, "data_store_addr_values"), table_words(f, "data_store_addr_bqtv")
        bqbase = table_words(f, "data_store_addr_bq")[0]
        for p in (0, 1):                                   # valid values frames, all biquads empty (type 0)
            frame = bytearray(fs); frame[0] = 1
            frame[cb - 1:cb + 1] = xmodem(frame[:cb - 1]).to_bytes(2, "little")
            at24[bases[p]:bases[p] + fs] = frame
            for nr in range(1, 565):
                at24[bqtvs[p] + (nr + 1) * 10 - 1] = 0
        for nr in (1, 200, 536):                           # three bands in preset 0 only
            at24[bqtvs[0] + (nr + 1) * 10 - 1] = 1
            data = bytes(rnd.randrange(256) for _ in range(20))
            a = bqbase + (nr - offs[core_t[nr]]) * 22
            dspee[core_t[nr]][a:a + 22] = xmodem(data[::-1]).to_bytes(2, "big") + data
        with tempfile.TemporaryDirectory() as d:
            lines = [f"#at24={pathlib.Path(d, 'at24.bin')}"]
            pathlib.Path(d, "at24.bin").write_bytes(bytes(at24))
            for n in (1, 2, 3):
                pathlib.Path(d, f"dsp{n}.bin").write_bytes(bytes(dspee[n]))
                lines.append(f"#dspee={n},{pathlib.Path(d, f'dsp{n}.bin')}")
            lines += ["#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#i2cdev=2,90", "#dsp_detect", "#dac_init",
                      "#preset_load=0", "#bq_count", "#preset_load=1", "#bq_count", "#preset_load=0", "#bq_count",
                      "#bq_forget", "#preset_load=1", "#bq_count"]
            p, _ = run(f, lines, eeprom=image(f), want_eeprom=True)
        counts = [tuple(int(x.split("=")[1]) for x in l.split(":")[1].split()) for l in p.stdout.splitlines()
                  if l.startswith("@bq_count:")]
        (w0, s0, m0), (w1, s1, m1), (w2, s2, m2), (w3, s3, m3) = counts
        self.assertGreater(w0, 500)                        # the first load writes every place (the host DSP is empty)
        self.assertEqual(s0, 0)
        self.assertEqual(m0, 0)
        self.assertEqual(w1 - w0, 3)                       # to preset 1: only the three bands change
        self.assertEqual(s1 - s0, w0 - 3)
        self.assertEqual(w2 - w1, 3)                       # and back
        # #67: cache unknown (as after a start), the DSP holds preset 0: read, only the three are written
        self.assertEqual(w3 - w2, 3)
        self.assertEqual(m3, w0 - 3)

    def test_96eln3_preset_0(self):
        import random
        import tempfile
        f = "V41-96ELN-3"
        rnd = random.Random(5)
        fs, cb = consts(f, "STORAGE_FRAMESIZE", "STORAGE_CHKSUM_BASE")
        vout, mute_b = consts(f, "STORAGE_VOL_OUT_BASE", "STORAGE_MUTE_OUT_BASE")
        core_t = table_bytes(f, "data_flow_dsp_bq_core")
        reg_t = table_bytes(f, "data_flow_dsp_bq_reg")
        offs = table_words(f, "data_flow_bq_storage_offset")
        at24 = bytearray(b"\xff" * 65536)
        dspee = {n: bytearray(b"\xff" * 65536) for n in (1, 2, 3)}
        # values frame of preset 0
        frame = bytearray(rnd.randrange(256) for _ in range(fs))
        for ch in range(8):
            frame[vout - 1 + ch] = 100 + ch
        frame[mute_b - 1] = 0x04          # channel 3 muted by the user
        frame[0] = 1                      # mixer position 1
        mux_b, = consts(f, "STORAGE_MUX_BASE")
        frame[mux_b - 1:mux_b + 3] = bytes([1, 2, 3, 4])  # valid mux entries
        frame[cb - 1:cb + 1] = xmodem(frame[:cb - 1]).to_bytes(2, "little")
        base = table_words(f, "data_store_addr_values")[0]
        at24[base:base + fs] = frame
        # biquads: nr 1, 200, 536 stored, the rest has type 0 (default)
        bqtv = table_words(f, "data_store_addr_bqtv")[0]
        bqbase = table_words(f, "data_store_addr_bq")[0]
        stored = {}
        for nr in (1, 200, 536):
            at24[bqtv + (nr + 1) * 10 - 1] = 1                      # store_load_bqtype
            data = bytes(rnd.randrange(256) for _ in range(20))     # bytes as sent to the DSP
            crc = xmodem(data[::-1])                                 # crc16(buffer_bytes(1..20))
            core = core_t[nr]
            a = bqbase + (nr - offs[core]) * 22
            dspee[core][a:a + 22] = crc.to_bytes(2, "big") + data
            stored[nr] = data
        for nr in range(1, 537):
            if nr not in stored:
                at24[bqtv + (nr + 1) * 10 - 1] = 0
        with tempfile.TemporaryDirectory() as d:
            lines = [f"#at24={pathlib.Path(d, 'at24.bin')}"]
            pathlib.Path(d, "at24.bin").write_bytes(bytes(at24))
            for n in (1, 2, 3):
                pathlib.Path(d, f"dsp{n}.bin").write_bytes(bytes(dspee[n]))
                lines.append(f"#dspee={n},{pathlib.Path(d, f'dsp{n}.bin')}")
            lines += ["#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#i2cdev=2,90", "#dsp_detect", "#dac_init",
                      "#preset_load=0"]
            adr = {1: (1, 0x68), 2: (2, 0x68), 3: (2, 0x6A)}
            for nr in (1, 2, 200, 536):
                bus, a = adr[core_t[nr]]
                lines.append(f"#block={bus},{a:02X},{reg_t[nr]:02X},20")
            ovd = table_bytes(f, "data_flow_dsp_output_vol_dsp")
            ovr = table_bytes(f, "data_flow_dsp_output_vol_reg")
            bus, a = adr[ovd[1]]
            lines.append(f"#block={bus},{a:02X},{ovr[1]:02X},4")
            lines.append("#regs=2,90,24")
            p, _ = run(f, lines, eeprom=image(f), want_eeprom=True)
        out = p.stdout
        self.assertIn("@preset:0 chksum_errors=0", out)
        blocks = [l[7:] for l in out.splitlines() if l.startswith("@block:")]
        self.assertEqual(blocks[0], stored[1].hex().upper())
        self.assertEqual(blocks[1], "0080000000000000000000000000000000000000")  # dsp_bq_default
        self.assertEqual(blocks[2], stored[200].hex().upper())
        self.assertEqual(blocks[3], stored[536].hex().upper())
        vol = table_bytes(f, "data_dsp_vol")
        self.assertEqual(blocks[4], vol[100 * 4:100 * 4 + 4][::-1].hex().upper())  # long, MSB first
        regs = bytes.fromhex([l for l in out.splitlines() if l.startswith("@regs:")][0][6:])
        volreg = table_bytes(f, "data_es90x8pro_vol_reg")
        self.assertEqual([regs[volreg[ch]] for ch in range(1, 9)], [0, 0, 0xFF, 0, 0, 0, 0, 0])
        self.assertNotIn("PIN_CPU_POWER_CTRL_OUT=1", out)


if __name__ == "__main__":
    unittest.main()


class PoolTest(unittest.TestCase):
    """#47: dynamic biquad pools; the MCU generator writes the same images as tools/dsp_pool.py"""
    F = "V40-96ELN"
    ORDER = [35, 36, 5, 6, 7, 8, 1, 2, 3, 4]   # pool order of dsp_pool_chan

    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, str(pathlib.Path(HERE, "..", "tools")))
        import dsp_pool
        t = Tas3108Test()
        cls.imgs = {n: t.program(cls.F, n) for n in (1, 2)}
        cls.progs, _ = dsp_pool.programs(str(pathlib.Path(HERE, "..", "src", "flavours", cls.F, "flavour_data.h")), cls.imgs)

    def device(self, lengths):
        ee = bytearray(image(self.F))
        if lengths:
            ee[0x0F20] = 0x47
            ee[0x0F21:0x0F21 + len(lengths)] = bytes(lengths)
        return bytes(ee)

    def test_images_match_reference(self):
        lengths = [100, 44, 8, 128, 31, 29, 189, 3, 1, 3]
        out, res = Tas3108Test().run_dsp(self.F, self.device(lengths), {n: b"\xff" * 65536 for n in (1, 2, 3)},
                                          ["#dsp_eeprom=1", "#dsp_eeprom=2", "#dsp_eeprom=3"], dump=(1, 2, 3))
        want = {1: self.progs[1].image(lengths[0:2]), 2: self.progs[2].image(lengths[2:6]),
                3: self.progs[2].image(lengths[6:10])}
        for n in (1, 2, 3):
            self.assertNotEqual(want[n], self.imgs[1 if n == 1 else 2])
            self.assertEqual(res[n][:len(want[n])], want[n], f"core {n}")

    def test_default_lengths_keep_the_program(self):
        for lengths in (None, [72, 72, 60, 60, 38, 38, 60, 60, 38, 38], [1, 1, 1, 1, 1, 1, 1, 1, 1, 1]):  # blank, default, invalid
            cores, ee, dsp = Tas3108Test().original_device(self.F)
            ee = bytearray(ee)
            ee[0x0F20:0x0F2B] = self.device(lengths)[0x0F20:0x0F2B]
            out, _ = Tas3108Test().run_dsp(self.F, bytes(ee), dsp, [f"#dsp_eeprom={n}" for n in (1, 2, 3)])
            for n in (1, 2, 3):
                self.assertRegex(out, rf"@dsp_eeprom{n}: hw=\w+ writes=0\n", lengths)

    def test_channel_limits_and_offsets(self):
        lengths = [100, 44, 8, 128, 31, 29, 189, 3, 1, 3]
        p, _ = run(self.F, BiquadCmdTest.PRE + ["1|get.1 biquad", "1|get.1 biquad.36", "1|set.1 biquad.36.45=[1]"],
                   eeprom=self.device(lengths), want_eeprom=True)
        lim = dict(zip(self.ORDER, lengths))
        self.assertIn("biquad=[" + ",".join(str(lim.get(i, 0)) for i in range(1, 37)) + "]", p.stdout)
        self.assertEqual(p.stdout.count("<.1 biquad.36."), 44)
        self.assertIn("out of range", p.stdout.split("biquad.36.45")[-1][:80])


class PoolCmdTest(unittest.TestCase):
    """#47: system.biquads; the stored presets move with their channel"""
    F = PoolTest.F

    def test_get_and_set(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            files = ["#at24out=%s/at24.bin" % d] + [f"#dspee_out={n},{d}/dsp{n}.bin" for n in (1, 2, 3)]
            # bands with values in preset 0: output 1 band 2, output 2 band 60, output 4 band 10 and 38
            p, ee = run(self.F, files + BiquadCmdTest.PRE + [
                "1|set.1 biquad.1.2=[1,1000,-35,7071,0,128]", "1|set.1 biquad.2.60=[1,2000,-40,7071,0,128]",
                "1|set.1 biquad.4.10=[1,2500,-42,7071,0,128]", "1|set.1 biquad.4.38=[1,3000,-45,7071,0,128]",
                "1|set.1 biquad.5.7=[1,3500,-30,7071,0,128]", "#mainloop",
                "1|get.1 system.biquads", "1|set.1 system.biquads.3=[60,60,38,38]",
                "1|set.1 system.biquads.3=[10,170,6,11]", "1|set.1 system.biquads.3=[60,60,38]",
                "1|set.1 system.biquads.3=[10,170,6,10]"], want_eeprom=True)
            out = p.stdout
            self.assertIn("system.biquads=[[35,1,72,1,172],[36,1,72,1,172],[5,2,60,3,196],[6,2,60,3,196],[7,2,38,1,196],"
                          "[8,2,38,3,196],[1,3,60,3,196],[2,3,60,3,196],[3,3,38,1,196],[4,3,38,3,196]]", out)
            self.assertEqual(out.count("command out of range"), 2)      # sum 197, and 3 values for 4 channels
            self.assertEqual(out.count("system.biquads.3 ok"), 2)       # unchanged lengths: ok without restart
            self.assertTrue(out.rstrip().endswith("@reset"))             # the change restarts
            self.assertEqual(ee[0x0F20], 0x47)
            self.assertEqual(list(ee[0x0F21:0x0F2B]), [72, 72, 60, 60, 38, 38, 10, 170, 6, 10])
            # the next start: limits, moved bands, the new DSP program of core 3 only
            back = ["#at24=%s/at24.bin" % d] + [f"#dspee={n},{d}/dsp{n}.bin" for n in (1, 2, 3)]
            p, _ = run(self.F, back + BiquadCmdTest.PRE[1:] + [
                "1|get.1 biquad", "1|get.1 biquad.1.2", "1|get.1 biquad.2.60", "1|get.1 biquad.2.170",
                "1|get.1 biquad.4.10", "1|get.1 biquad.1.11", "1|get.1 biquad.5.7", "1|get.1 system.biquads"],
                eeprom=bytes(ee), want_eeprom=True)
        out = p.stdout
        self.assertIn("biquad=[10,170,6,10,60,60,38,38,", out)
        self.assertIn("biquad.1.2=[1,1000,-35,7071,0,128]", out)        # kept
        self.assertIn("biquad.2.60=[1,2000,-40,7071,0,128]", out)       # moved down by 50 virtual numbers
        self.assertIn("biquad.2.170=[0,0,0,0,0,0]", out)                 # new band: default as after preset erase
        self.assertIn("biquad.4.10=[1,2500,-42,7071,0,128]", out)       # moved up
        self.assertNotIn("3000", out)                                   # band 38 of output 4 dropped
        self.assertIn("biquad.5.7=[1,3500,-30,7071,0,128]", out)        # other core untouched
        self.assertIn("!.1 biquad.1.11 command out of range", out)
        self.assertIn("[1,3,10,3,196],[2,3,170,3,196],[3,3,6,1,196],[4,3,10,3,196]]", out)


class PoolSpareTest(unittest.TestCase):
    """#47 step 7: 28 spare biquads in core 1 (virtual numbers 537 …)"""
    F = PoolTest.F

    def test_spares(self):
        lengths = [100, 72, 60, 60, 38, 38, 60, 60, 38, 38]
        ee = PoolTest().device(lengths)
        out, res = Tas3108Test().run_dsp(self.F, ee, {n: b"\xff" * 65536 for n in (1, 2, 3)}, ["#dsp_eeprom=1"], dump=(1,))
        PoolTest.setUpClass()
        want = PoolTest.progs[1].image(lengths[0:2])
        self.assertEqual(res[1][:len(want)], want)
        p, _ = run(self.F, BiquadCmdTest.PRE + ["1|get.1 biquad", "1|set.1 biquad.35.100.calc=[1,1000,-35,7071,0,128]",
                                                "1|get.1 biquad.35.100", "1|set.1 system.biquads.1=[100,73]"],
                   eeprom=ee, want_eeprom=True)
        self.assertIn(",100,72]", p.stdout)
        self.assertIn("biquad.35.100=[1,1000,-35,7071,0,128]", p.stdout)
        self.assertIn("system.biquads.1 command out of range", p.stdout)      # 173 > 172
        self.assertNotIn("system.errors", p.stdout)
