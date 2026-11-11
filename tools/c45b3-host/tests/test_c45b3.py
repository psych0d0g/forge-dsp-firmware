import os
import random
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from c45b3 import ihex, protocol as P  # noqa: E402
from c45b3.client import (  # noqa: E402
    APP_SIZE, EEPROM_SIZE, PAGE_SIZE, Bootloader, NotSupported, Refused,
)
from c45b3.sim import SimPort, Target, TargetFault  # noqa: E402
from ports import CorruptingPort, PipePort, ctarget_path  # noqa: E402

# checkout of bitbucket.org/forge-dsp/firmware (optional)
FORGE_FW = os.environ.get("FORGE_FIRMWARE_DIR", "../forge-dsp/firmware")
FORGE_HEX = os.path.join(FORGE_FW, "devices/V41/96ELN-3/BQ8x60_38/main.hex")
FORGE_BIN = FORGE_HEX[:-3] + "bin"


def rand_bytes(n, seed):
    r = random.Random(seed)
    return bytes(r.randrange(256) for _ in range(n))


class ProtocolTest(unittest.TestCase):
    def test_crc_is_ccitt_false(self):
        # augmented start 0x84CF == direct-form init 0xFFFF; check value of CRC-16/CCITT-FALSE
        self.assertEqual(P.crc16(b"123456789"), 0x29B1)
        self.assertEqual(P.crc16(b""), 0xFFFF)

    def test_escape(self):
        f = P.encode_frame(bytes([0x22, 0x02, 0x03, 0x1B, 0x82]))
        self.assertEqual(f[0], P.STX)
        self.assertEqual(f[-1], P.ETX)
        self.assertEqual(f[1:9], bytes([0x22, 0x1B, 0x82, 0x1B, 0x83, 0x1B, 0x9B, 0x82]))
        inner = f[1:-1]
        self.assertNotIn(P.STX, inner)
        self.assertNotIn(P.ETX, inner)

    def test_roundtrip_all_bytes(self):
        for seed in range(50):
            payload = rand_bytes(65, seed)
            d = P.FrameDecoder()
            out = None
            for c in P.encode_frame(payload):
                r = d.feed(c)
                if r is not None:
                    out = r
            self.assertEqual(out, payload)

    def test_decoder_crc_error(self):
        f = bytearray(P.encode_frame(b"\x91\x01\x00"))
        f[2] ^= 0x10
        d = P.FrameDecoder()
        with self.assertRaises(P.CrcError):
            for c in f:
                d.feed(c)


class IhexTest(unittest.TestCase):
    def test_roundtrip(self):
        data = rand_bytes(0x12345, 1)
        with tempfile.TemporaryDirectory() as t:
            p = os.path.join(t, "x.hex")
            ihex.write_hex(p, data)
            self.assertEqual(ihex.to_bin(ihex.read_hex(p)), data)

    @unittest.skipUnless(os.path.exists(FORGE_HEX), "Forge firmware repo not available")
    def test_forge_release_hex(self):
        img = ihex.to_bin(ihex.read_hex(FORGE_HEX))
        self.assertEqual(img, open(FORGE_BIN, "rb").read())
        self.assertEqual(len(img), 205728)
        self.assertEqual(img[:4], bytes.fromhex("0c94fe00"))


class Scenarios:
    """Run against the Python model and against the original C sources."""

    def make(self, **kw):
        raise NotImplementedError

    def finish(self, port):
        """-> (flash, eeprom, boot_page_writes)"""
        raise NotImplementedError

    def bl(self, port):
        b = Bootloader(port, timeout=0.5, slow_timeout=2.0)
        self.assertTrue(b.connect(attempts=3, attempt_time=0.3))
        return b

    def test_versions(self):
        img = bytearray(rand_bytes(4096, 2))
        img[1000:1016] = b"FW_VERSION:11.60"
        port = self.make(flash=bytes(img))
        b = self.bl(port)
        self.assertEqual(b.bootloader_version(), (1, 0))
        self.assertEqual(b.firmware_version(), (11, 60))
        b.start_application()
        self.finish(port)

    def test_firmware_version_absent(self):
        port = self.make(flash=b"\x0c\x94" * 100)
        b = self.bl(port)
        self.assertIsNone(b.firmware_version())
        self.finish(port)

    def test_read_flash_and_eeprom_full(self):
        flash = rand_bytes(APP_SIZE, 3)
        eeprom = rand_bytes(EEPROM_SIZE, 4)
        port = self.make(flash=flash, eeprom=eeprom)
        b = self.bl(port)
        self.assertEqual(b.read_flash(), flash)  # stops at BOOT_SECTION_START
        self.assertEqual(b.read_eeprom(), eeprom)
        self.assertEqual(b.read_flash(start=0x3FFF0, size=100), flash[0x3FFF0:])
        self.finish(port)

    def test_resume_after_stay(self):
        port = self.make()
        b = self.bl(port)
        b2 = Bootloader(port, timeout=0.5)
        self.assertFalse(b2.connect(attempts=1, attempt_time=0.2))  # no identifier again
        self.assertTrue(b2.resume())
        b2.start_application()
        self.finish(port)

    def test_address_in_boot_section_refused(self):
        port = self.make()
        b = self.bl(port)
        with self.assertRaises(Refused):
            b.set_address(APP_SIZE)
        self.finish(port)

    def test_write_flash_release_size(self):
        old = rand_bytes(APP_SIZE, 5)
        img = rand_bytes(205728, 6)
        port = self.make(flash=old)
        b = self.bl(port)
        n = b.write_flash(img)
        self.assertEqual(n % PAGE_SIZE, 0)
        self.assertEqual(b.read_flash(size=n), img + b"\xFF" * (n - len(img)))
        flash, _, boot = self.finish(port)
        self.assertEqual(flash[:len(img)], img)
        self.assertEqual(flash[len(img):n], b"\xFF" * (n - len(img)))
        self.assertEqual(flash[n:APP_SIZE], old[n:])  # rest untouched
        self.assertEqual(flash[APP_SIZE:APP_SIZE + 7], b"c45b3BL")
        self.assertEqual(boot, 0)

    def test_write_flash_up_to_boot_section(self):
        img = rand_bytes(APP_SIZE, 7)
        port = self.make()
        b = self.bl(port)
        b.write_flash(img)
        flash, _, boot = self.finish(port)
        self.assertEqual(flash[:APP_SIZE], img)
        self.assertEqual(boot, 0, "tool must not touch the boot section")

    def test_gui_final_empty_frame_hits_boot_section(self):
        """Documents the GUI sequence: page-aligned image up to BOOT_SECTION_START
        plus the final empty 0x22 frame -> page write at 0x40000."""
        port = self.make()
        b = self.bl(port)
        b.write_flash(rand_bytes(APP_SIZE, 8))
        b.transact(bytes([P.CMD_FLASH_WRITE_DATA]))  # what the GUI sends at the end
        _, _, boot = self.finish(port)
        self.assertEqual(boot, 1)

    def test_write_eeprom(self):
        old = rand_bytes(EEPROM_SIZE, 9)
        data = rand_bytes(1000, 10)
        port = self.make(eeprom=old)
        b = self.bl(port)
        b.write_eeprom(data, start=0x100)
        self.assertEqual(b.read_eeprom(start=0x100, size=1000), data)
        _, eeprom, _ = self.finish(port)
        self.assertEqual(eeprom, old[:0x100] + data + old[0x100 + 1000:])

    def test_target_crc_error_retry(self):
        port = self.make()
        cp = CorruptingPort(port, tx_frame=1)  # first frame after connect
        b = self.bl(cp)
        self.assertEqual(b.bootloader_version(), (1, 0))
        self.assertEqual(b.target_crc_errors, 1)
        self.finish(port)

    def test_answer_crc_error_resync(self):
        flash = rand_bytes(APP_SIZE, 11)
        port = self.make(flash=flash)
        # rx frame 1 = identifier, 2 = set address, 3.. = read answers
        cp = CorruptingPort(port, rx_frame=5)
        b = self.bl(cp)
        self.assertEqual(b.read_flash(size=1024), flash[:1024])
        self.finish(port)


class SimTest(Scenarios, unittest.TestCase):
    def make(self, flash=None, eeprom=None, **kw):
        return SimPort(Target(flash=flash, eeprom=eeprom, **kw))

    def finish(self, port):
        t = port.target
        return bytes(t.flash), bytes(t.eeprom), t.boot_write_attempts

    def test_reads_not_compiled_in(self):
        port = self.make(flash_read=False, eeprom_read=False)
        b = self.bl(port)
        with self.assertRaises(NotSupported):
            b.read_flash()
        with self.assertRaises(NotSupported):
            b.read_eeprom()

    def test_connect_while_power_cycling(self):
        t = Target(in_bootloader=False)
        port = SimPort(t)
        writes = [0]
        orig = port.write

        def write(data):
            writes[0] += 1
            if writes[0] == 5:
                t.reset()  # user switches the Forge off and on
            return orig(data)
        port.write = write
        b = Bootloader(port, timeout=0.5)
        self.assertTrue(b.connect(wait=5, attempt_time=0.05))
        self.assertEqual(b.bootloader_version(), (1, 0))
        self.assertTrue(t.app_rx.startswith(b"U" * 16))

    def test_enter_command_and_backup(self):
        import io
        import contextlib
        import c45b3.__main__ as cli
        from c45b3 import sim
        t = Target(in_bootloader=False, flash_read=False)
        orig = cli.open_port
        cli.open_port = lambda args: SimPort(t)
        try:
            with tempfile.TemporaryDirectory() as d:
                with contextlib.redirect_stdout(io.StringIO()):
                    cli.main(["--sim", "--enter", ">system.bootloader=1", "backup",
                              os.path.join(d, "bk")])
                self.assertTrue(os.path.exists(os.path.join(d, "bk", "eeprom.bin")))
                self.assertFalse(os.path.exists(os.path.join(d, "bk", "flash.bin")))
                self.assertIn(b">system.bootloader=1\r\n", bytes(t.app_rx))
                self.assertEqual(t.state, "exited")
        finally:
            cli.open_port = orig
        del sim

    def test_device_check(self):
        import io
        import contextlib
        import c45b3.__main__ as cli
        orig = cli.open_port
        try:
            for key, expect, ok in ((b"FAG-96ELN3-4.1-x-CCBYNCSA-20241206-MDD", "96ELN3-4.1", True),
                                    (b"FAG-CORED8-4.1-x-CCBYNCSA-20241206-MDD", "96ELN3-4.1", False),
                                    (b"FAG-96ELN3-4.0-x-CCBYNCSA-20241206-MDD", "96ELN3-4.1", False),
                                    (None, "96ELN3-4.1", False)):
                t = Target(in_bootloader=False, key_info=key)
                cli.open_port = lambda args, t=t: SimPort(t)
                argv = ["--sim", "--expect-device", expect, "--enter", ">system.bootloader=1", "info"]
                with contextlib.redirect_stdout(io.StringIO()):
                    if ok:
                        cli.main(argv)
                        self.assertEqual(t.state, "exited")
                    else:
                        with self.assertRaises(SystemExit):
                            cli.main(argv)
                        self.assertEqual(t.state, "app")  # never entered the bootloader
        finally:
            cli.open_port = orig

    def test_parse_key_info(self):
        from c45b3.__main__ import parse_key_info
        self.assertEqual(parse_key_info(' <.2 system.key.info="FAG-CORED8-4.0-1A2B-CCBYNCSA-20241206-MDD"\r\n'),
                         ("CORED8", "4.0"))
        self.assertIsNone(parse_key_info(" !.2 unknown command"))

    def test_unlocked_boot_would_be_damaged(self):
        t = Target(boot_locked=False)
        b = self.bl(SimPort(t))
        b.write_flash(rand_bytes(APP_SIZE, 12))
        self.assertFalse(t.boot_damaged)
        b.transact(bytes([P.CMD_FLASH_WRITE_DATA]))
        self.assertTrue(t.boot_damaged)

    def test_short_frame_fault(self):
        t = Target()
        b = self.bl(SimPort(t))
        with self.assertRaises(TargetFault):
            t.rx(bytes([P.STX, P.ETX]))
        del b

    def test_cli(self):
        from c45b3.__main__ import main
        with tempfile.TemporaryDirectory() as d:
            main(["--sim", "info"])
            base = os.path.join(d, "dump")
            main(["--sim", "read-eeprom", base])
            self.assertEqual(open(base + ".bin", "rb").read(), b"\xFF" * EEPROM_SIZE)
            with self.assertRaises(SystemExit):
                main(["--sim", "read-eeprom", base])  # refuses to overwrite
            img = os.path.join(d, "img.bin")
            open(img, "wb").write(rand_bytes(3000, 13))
            with self.assertRaises(SystemExit):
                main(["--sim", "write-flash", img])  # no --allow-write
            main(["--sim", "write-flash", img, "--allow-write"])
            with self.assertRaises(SystemExit):
                main(["--sim", "--sim-no-read", "read-flash", os.path.join(d, "f")])


@unittest.skipUnless(ctarget_path("full"), "native C target not built (make -C tests/ctarget)")
class CTargetTest(Scenarios, unittest.TestCase):
    variant = "full"

    def make(self, flash=None, eeprom=None):
        return PipePort(ctarget_path(self.variant), flash=flash, eeprom=eeprom)

    def finish(self, port):
        port.close()
        boot = sum(1 for line in port.stderr.splitlines() if line.startswith("BOOTPAGE"))
        return port.flash, port.eeprom, boot


@unittest.skipUnless(ctarget_path("example"), "native C target not built")
class CTargetExampleBuildTest(unittest.TestCase):
    """Build flags of Xmega_Example.cproj: no read commands."""

    def test_reads_not_answered(self):
        port = PipePort(ctarget_path("example"))
        b = Bootloader(port, timeout=0.5, slow_timeout=2.0)
        self.assertTrue(b.connect(attempts=3))
        self.assertEqual(b.bootloader_version(), (1, 0))
        with self.assertRaises(NotSupported):
            b.read_flash()
        with self.assertRaises(NotSupported):
            b.read_eeprom()
        b.start_application()
        port.close()
        self.assertIn("START_APPLICATION", port.stderr)


@unittest.skipUnless(ctarget_path("full"), "native C target not built")
class ModelEquivalenceTest(unittest.TestCase):
    """Same raw byte stream into model and C target -> same bytes out."""

    def test_raw_stream(self):
        flash = bytearray(rand_bytes(APP_SIZE, 20))
        flash[0x2000:0x2010] = b"FW_VERSION:01.11"
        eeprom = rand_bytes(EEPROM_SIZE, 21)
        frames = [P.encode_frame(bytes([P.CMD_READ_VERSION_BOOTLOADER])),
                  P.encode_frame(bytes([P.CMD_READ_VERSION_FIRMWARE])),
                  P.encode_frame(bytes([P.CMD_SET_ADDRESS, 0, 3, 0xFF, 0xC0])),
                  P.encode_frame(bytes([P.CMD_FLASH_READ_DATA, 64])),
                  P.encode_frame(bytes([P.CMD_FLASH_READ_DATA, 64])),
                  P.encode_frame(bytes([P.CMD_SET_ADDRESS, 0, 4, 0, 0])),
                  P.encode_frame(bytes([0x13])),
                  P.encode_frame(bytes([P.CMD_SET_ADDRESS, 0, 0, 0x0F, 0xF0])),
                  P.encode_frame(bytes([P.CMD_EEPROM_READ_DATA, 100])),
                  b"UUU" + P.encode_frame(bytes([P.CMD_SET_ADDRESS, 0, 0, 2, 0])),
                  P.encode_frame(bytes([P.CMD_FLASH_WRITE_DATA]) + rand_bytes(64, 22)),
                  bytes([P.STX, 0x11, 0x00, 0x00, P.ETX]),  # bad CRC -> "e0"
                  P.encode_frame(bytes([P.CMD_FLASH_WRITE_DATA]))]
        import time
        cport = PipePort(ctarget_path("full"), flash=bytes(flash), eeprom=eeprom)
        sport = SimPort(Target(flash=bytes(flash), eeprom=eeprom))
        outs = []
        for port in (sport, cport):
            got = bytearray()
            for chunk in [b"U" * 16] + frames:
                port.write(chunk)
                t_end = time.monotonic() + (1.0 if port is cport else 0)
                while True:
                    d = port.read(4096)
                    if d:
                        got += d
                        t_end = time.monotonic() + (0.1 if port is cport else 0)
                    elif time.monotonic() >= t_end:
                        break
            outs.append(bytes(got))
        cport.close()
        self.assertGreater(len(outs[0]), 100)
        self.assertEqual(outs[0], outs[1])
        self.assertEqual(cport.flash, bytes(sport.target.flash))
        self.assertEqual(cport.eeprom, bytes(sport.target.eeprom))


if __name__ == "__main__":
    unittest.main()
