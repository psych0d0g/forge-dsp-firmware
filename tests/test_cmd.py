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
                # not have, and not the DAC/SPDIF bits (2, 3) it detects -> mismatch (issue #32)
                self.assertEqual(other, 0x0871)
                self.assertEqual(line, f"@hw:007D dsp:{dsp:04X}", f)
                continue
            self.assertEqual(line, f"@hw:{other:04X} dsp:{dsp:04X}", f)

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
        gain = gain * 10
        l = int(gain)
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
            self.assertEqual(lines[0], f"@i2c{bus}: S {adr:02X} {reg:02X} " + " ".join(f"{x:02X}" for x in data) + " P", nr)
            self.assertTrue(lines[1].startswith(f"@i2c{bus}: S {adr:02X} {reg:02X} Sr {adr + 1:02X}"), nr)
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
            b = nout // ndsp
            vals = [0, 50, 100, 240] + [0] * (nout - 4)
            left = [samples - sum(vals[i * b:(i + 1) * b]) for i in range(ndsp)]
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


@unittest.skipUnless(all(available(f) for f in FLAVOURS), "native harness not built")
class BiquadCmdTest(LinesMixin, unittest.TestCase):
    """cmd_biquad: stored values, ranges, calc and raw into DSP and DSP EEPROM."""

    PRE = ["#store_init", "#i2cdsp=1,68", "#i2cdsp=2,68", "#i2cdsp=2,6A", "#dsp_detect",
           "1|set.1 preset.loaded=0", "#mainloop"]

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
    """M6 (#6): MODC type 0x6C loudness on the last two biquads of channels 35/36."""

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
            cfg = "1|set.1 modc.9=[1,108,00232400,70,0,0,0,DF280F18]"   # ref 223, 40 %, 15 %, max 12 dB
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

    def test_follows_gain_spdif(self):
        f = "V40-96ELN"
        reads = self.blocks(f, 35, None)
        out = self.lines(f, self.PRE + ["1|set.1 gain.spdif.1=223",                          # 0 dB: no boost
                                        "1|set.1 modc.9=[1,108,00000000,0,0,0,0,00000000]",  # all defaults
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
        out = self.lines(f, self.PRE + ["1|set.1 modc.9=[1,108,00232400,70,0,0,0,DF64001E]",  # 100 %, max 30 -> 24
                                        "1|set.1 event.70.value=100", "#modc"] + reads)
        b = [l[7:] for l in out if l.startswith("@block:")]
        self.assertEqual(b[0], self.ref(f, 14, 100, 120))                # capped at 12 dB
        self.assertEqual(b[1], self.ref(f, 15, 10000, 92))               # set 3 = 0: default 15 % of 61.5 dB


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
