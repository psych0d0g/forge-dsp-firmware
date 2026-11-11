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


def consts(flavour, *names):
    import re
    t = pathlib.Path(HERE, "..", "src", "flavours", flavour, "flavour.h").read_text()
    return [int(re.search(rf"#define BAS_{n} (\d+)", t).group(1)) for n in names]


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
        code = table_bytes(f, "dsp_1_code" if n == 1 else "dsp_2_code")
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

    def test_mismatch_rewrites_program(self):
        f = "V41-96ELN-3"
        cores, ee, dsp = self.original_device(f)
        dsp[3] = b"\x00" * 65536  # damaged program EEPROM of core 3
        out, res = self.run_dsp(f, ee, dsp, ["#dsp_eeprom=3"], dump=(3,))
        prog = self.program(f, 3)
        self.assertIn("@dsp_eeprom3: hw=0020 writes=56526", out)  # 32001 x 0xFF + program
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


if __name__ == "__main__":
    unittest.main()
