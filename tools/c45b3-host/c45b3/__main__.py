"""chip45boot3 command line tool for Linux.

  python -m c45b3 --port /dev/ttyUSB0 info
  python -m c45b3 --port /dev/ttyUSB0 --wait 30 read-flash backup/forge-flash
  python -m c45b3 --sim --sim-image main.hex info  (built-in target model)

Reading is the default use. Writing needs --allow-write (and the user's
approval of the device owner).
"""

import argparse
import hashlib
import os
import time
import sys

from . import client, ihex
from .client import Bootloader, BootloaderError, NotSupported


def log(msg):
    print(msg, file=sys.stderr)


def open_port(args):
    if args.sim:
        from .sim import SimPort, Target
        flash = load_image(args.sim_image) if args.sim_image else None
        t = Target(flash=flash, flash_read=not args.sim_no_read, eeprom_read=not args.sim_no_read)
        return SimPort(t)
    import serial  # pyserial
    p = serial.Serial(args.port, args.baud, bytesize=8, parity="N", stopbits=1,
                      timeout=0.02, write_timeout=2, rtscts=False, dsrdtr=False, xonxoff=False)
    try:
        p.dtr = True  # like the GUI (DtrEnable) and the Forge GUI (DTR=1)
    except OSError:
        log("note: port has no DTR line (pty?)")
    return p


def reopener(args):
    """For --wait: reopen the port after the CP2102 disappeared (Forge switched off/on)."""
    if args.sim:
        return None
    import time

    def reopen(old, deadline):
        try:
            old.close()
        except OSError:
            pass
        while time.monotonic() < deadline:
            try:
                p = open_port(args)
                log("port reopened")
                return p
            except OSError:
                time.sleep(0.2)
        return None
    return reopen


def load_image(path):
    if path.lower().endswith(".hex"):
        return ihex.to_bin(ihex.read_hex(path))
    with open(path, "rb") as f:
        return f.read()


def save_dump(base, data):
    paths = [base + ".bin", base + ".hex"]
    for p in paths:
        if os.path.exists(p):
            raise SystemExit(f"{p} exists, not overwriting")
    d = os.path.dirname(base)
    if d:
        os.makedirs(d, exist_ok=True)
    with open(paths[0], "wb") as f:
        f.write(data)
    ihex.write_hex(paths[1], data)
    used = len(data.rstrip(b"\xFF"))
    print(f"{len(data)} bytes, used up to 0x{used:X}, sha256 {hashlib.sha256(data).hexdigest()}")
    print("written:", *paths)


def progress_printer(total):
    last = [-1]

    def p(done):
        pct = done * 100 // total if total else 0
        if pct // 5 != last[0]:
            last[0] = pct // 5
            print(f"\r{done:7d} / {total} bytes", end="", file=sys.stderr, flush=True)
    return p


def cmd_info(bl, args):
    print("bootloader version: %d.%d" % bl.bootloader_version())
    try:
        v = bl.firmware_version()
        print("firmware version:   " + ("%d.%02d" % v if v else "no FW_VERSION: string in application"))
    except NotSupported as e:
        print("firmware version:   not supported (" + str(e) + ")")


def cmd_read(bl, args, what):
    size = args.size
    total = size or (client.APP_SIZE if what == "flash" else client.EEPROM_SIZE)
    reader = bl.read_flash if what == "flash" else bl.read_eeprom
    try:
        data = reader(start=args.start, size=size, progress=progress_printer(total))
    except NotSupported as e:
        print(file=sys.stderr)
        raise SystemExit(f"{what} read not available: {e}")
    print(file=sys.stderr)
    save_dump(args.out, data)


def cmd_write(bl, args, what):
    if not args.allow_write:
        raise SystemExit("writing needs --allow-write (only with the user's explicit approval)")
    data = load_image(args.image)
    print(f"{what}: {len(data)} bytes from {args.image}, sha256 {hashlib.sha256(data).hexdigest()}")
    if what == "flash":
        n = bl.write_flash(data, start=args.start, progress=progress_printer(len(data)))
    else:
        n = bl.write_eeprom(data, start=args.start, progress=progress_printer(len(data)))
    print(file=sys.stderr)
    print(f"{n} bytes written, verified by the target page by page")
    if args.verify:
        try:
            back = (bl.read_flash if what == "flash" else bl.read_eeprom)(start=args.start, size=n)
        except NotSupported:
            print("read-back not available in this bootloader build")
            return
        exp = data + b"\xFF" * (n - len(data))
        print("read-back:", "OK" if back == exp else "MISMATCH")
        if back != exp:
            raise SystemExit(1)


def app_query(port, args, line, wait=0.5):
    """Send one text-protocol line at the application baud rate, return the answer."""
    import time
    try:
        port.baudrate = args.app_baud
    except AttributeError:
        pass
    port.reset_input_buffer()
    port.write(line.encode("ascii") + b"\r\n")
    t_end = time.monotonic() + wait
    ans = bytearray()
    while time.monotonic() < t_end:
        ans += port.read(64)
    try:
        port.baudrate = args.baud
    except AttributeError:
        pass
    return ans.decode("latin-1")


def parse_key_info(text):
    """' <.1 system.key.info="FAG-96ELN3-4.1-<mcuid>-CCBYNCSA-<date>-MDD"' -> ("96ELN3", "4.1")"""
    import re
    m = re.search(r'system\.key\.info="([^"]*)"', text)
    if not m:
        return None
    parts = m.group(1).split("-")
    if len(parts) < 3:
        return None
    return parts[1], parts[2]


def check_clock(port, args):
    """--expect-clock si5338|none (#10): V30 and V40 boards report the same identity (96ELN-4.0);
    only V40 boards carry the Si5338 clock generator, at 0xE0 or 0xE2 on I2C bus 1 (the firmware
    probes both). get.1 i2c.1.scan is
    answered by the original firmware and the port alike."""
    import re
    ans = app_query(port, args, "get.1 i2c.1.scan", wait=1.0)
    m = re.search(r"i2c\.1\.scan=\[([^\]]*)\]", ans)
    if not m:
        raise SystemExit(f"board check: no answer to get.1 i2c.1.scan ({ans.strip()!r}). Not flashing.")
    found = m.group(1).upper()
    has = "&HE0" in found or "&HE2" in found
    want = args.expect_clock == "si5338"
    if has != want:
        raise SystemExit("board check: Si5338 " + ("found" if has else "missing") + " on I2C bus 1, this build is for "
                         + ("a V40 board (with Si5338)" if want else "a V30 board (without Si5338)") + ". Not flashing.")
    log("board check: Si5338 " + ("present (V40 board)" if has else "absent (V30 board)") + " ok")


def check_device(port, args):
    """--expect-device MODEL-PCB[,MODEL-PCB...]: one build may serve several identities (#31)."""
    allowed = [tuple(x.strip().split("-", 1)) for x in args.expect_device.split(",") if x.strip()]
    ans = app_query(port, args, "get.1 system.key.info")
    got = parse_key_info(ans)
    if got is None:
        raise SystemExit("device check: no answer to get.1 system.key.info "
                         f"({ans.strip()!r}). Forge on and running its firmware? "
                         "(skip the check only if you are sure: no --expect-device)")
    if got not in allowed:
        raise SystemExit(f"device check: Forge reports {got[0]} PCB {got[1]}, this build is for "
                         + ", ".join("-".join(a) for a in allowed) + ". Not flashing.")
    log(f"device check: {got[0]} PCB {got[1]} ok")
    args.identity = "-".join(got)
    return got


def restore_identity(port, args, timeout=90.0):
    """After flashing a build that serves several identities: tell the new firmware which one
    the device is (set.1 system.key.info="MODEL-PCB", kept in its EEPROM)."""
    import time
    ident = getattr(args, "identity", None)
    if not ident:
        return
    t_end = time.monotonic() + timeout
    while time.monotonic() < t_end:  # the start (preset load) takes some seconds
        if "pong" in app_query(port, args, "get.1 system.ping", wait=0.5):
            break
    else:
        raise SystemExit("identity: the new firmware does not answer; set it by hand: "
                         f'set.1 system.key.info="{ident}"')
    got = None
    while time.monotonic() < t_end and not got:  # ping answers before the preset load blocks it
        got = parse_key_info(app_query(port, args, "get.1 system.key.info", wait=1.0))
        if not got:
            time.sleep(1)
    if got and "-".join(got) == ident:
        log(f"identity: {ident} (unchanged)")
        return
    ans = app_query(port, args, f'set.1 system.key.info="{ident}"')
    got = parse_key_info(app_query(port, args, "get.1 system.key.info"))
    if not got or "-".join(got) != ident:
        raise SystemExit(f"identity: could not set {ident} ({ans.strip()!r}); "
                         "the original firmware does not know this command")
    log(f"identity: {ident} set")


# The port firmware resets about 1 s after "set.1 system.boot"; the bootloader then calibrates
# its baud rate on the first character it sees. Bursts sent while the application is still
# running reach it half-way and it locks onto a wrong rate, so wait until the reset is over.
ENTER_SETTLE = 2.0


def enter_bootloader(port, args, settle=ENTER_SETTLE):
    """Ask the running application to reset into the bootloader (port firmware: the bootloader
    sequence in the EEPROM, see docs/bootloader.md; the original only restarts)."""
    ans = app_query(port, args, args.enter, wait=0.3)
    if ans.strip():
        log("application answered: " + ans.strip())
    time.sleep(settle)
    port.reset_input_buffer()


def cmd_tuning(port, args):
    from . import tuning
    app = tuning.App(port, args.app_baud, log)
    if args.cmd == "export-tuning":
        model, entries = tuning.export(app)
        stamp = time.strftime("%Y-%m-%d %H:%M:%S")
        with open(args.out, "w", encoding="latin-1") as f:
            f.write(tuning.to_cfg(model, entries, [f"Forge tuning export {stamp}, c45b3"]))
        log(f"tuning: {len(entries)} values of {model} -> {args.out}")
        return
    with open(args.cfg, encoding="latin-1") as f:
        model, entries = tuning.parse_cfg(f.read())
    if args.cmd == "compare-tuning":
        have_model, have = tuning.export(app)
        diff = tuning.compare(entries, have)
        for reg, a, b in diff:
            print(f"{reg}: file {a} device {b}")
        log(f"tuning: {len(diff)} differences ({model} file, {have_model} device)")
        if diff:
            raise SystemExit(1)
        return
    if not args.allow_write:
        raise SystemExit("restore-tuning writes to the device: add --allow-write")
    written, errors = tuning.restore(app, model, entries, log)
    log(f"tuning: {written} values written, {errors} errors")
    if errors:
        raise SystemExit(1)


def save_tuning(port, args, tag):
    """Export before an update; returns (model, entries) or None when the firmware does not answer."""
    from . import tuning
    os.makedirs(args.keep_tuning, exist_ok=True)
    app = tuning.App(port, args.app_baud, log)
    try:
        model, entries = tuning.export(app)
    except SystemExit as e:
        log(f"tuning: no export ({e}); continuing without")
        return None
    finally:
        try:
            port.baudrate = args.baud
        except AttributeError:
            pass
    path = os.path.join(args.keep_tuning, f"tuning-{tag}.cfg")
    with open(path, "w", encoding="latin-1") as f:
        f.write(tuning.to_cfg(model, entries, [f"Forge tuning {tag} update {time.strftime('%Y-%m-%d %H:%M:%S')}"]))
    log(f"tuning: {len(entries)} values saved to {path}")
    return model, entries


def check_tuning(port, args, before):
    """After the start of the new firmware: export again, compare, optionally write back."""
    from . import tuning
    app = tuning.App(port, args.app_baud, log)
    if not app.ready():
        raise SystemExit("tuning: the new firmware does not answer; the export before the update is "
                         f"in {args.keep_tuning}")
    model, entries = tuning.export(app)
    with open(os.path.join(args.keep_tuning, "tuning-after.cfg"), "w", encoding="latin-1") as f:
        f.write(tuning.to_cfg(model, entries, ["Forge tuning after update"]))
    diff = tuning.compare(before[1], entries)
    if not diff:
        log("tuning: unchanged by the update")
        return
    for reg, a, b in diff[:20]:
        log(f"tuning: {reg}: before {a} now {b}")
    log(f"tuning: {len(diff)} values differ after the update")
    if not args.restore_tuning:
        log(f"tuning: write them back with: restore-tuning {args.keep_tuning}/tuning-before.cfg --allow-write")
        return
    tuning.restore(app, before[0], before[1], log)
    _, again = tuning.export(app)
    left = tuning.compare(before[1], again)
    log(f"tuning: written back, {len(left)} differences left")


def cmd_backup(bl, args):
    import datetime
    import json
    d = args.dir
    if os.path.exists(d) and os.listdir(d):
        raise SystemExit(f"{d} is not empty")
    os.makedirs(d, exist_ok=True)
    meta = {"date": datetime.datetime.now().isoformat(timespec="seconds"),
            "bootloader_version": "%d.%d" % bl.bootloader_version()}
    try:
        v = bl.firmware_version()
        meta["firmware_version"] = "%d.%02d" % v if v else None
    except NotSupported:
        meta["firmware_version"] = "not supported"
    for what, reader, total in (("flash", bl.read_flash, client.APP_SIZE),
                                ("eeprom", bl.read_eeprom, client.EEPROM_SIZE)):
        try:
            data = reader(progress=progress_printer(total))
            print(file=sys.stderr)
            save_dump(os.path.join(d, what), data)
            meta[what] = {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        except NotSupported as e:
            print(file=sys.stderr)
            print(f"{what}: not readable with this bootloader ({e})")
            meta[what] = "not supported"
    with open(os.path.join(d, "backup.json"), "w") as f:
        json.dump(meta, f, indent=2)
    print("backup in", d)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="c45b3", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="serial device, e.g. /dev/ttyUSB0 (CP2102 of the Forge)")
    src.add_argument("--sim", action="store_true", help="use the built-in target model")
    ap.add_argument("--sim-image", metavar="IMAGE", help="flash content of the model (.hex/.bin)")
    ap.add_argument("--sim-no-read", action="store_true",
                    help="model a build without PROVIDE_FLASH_READ/PROVIDE_EEPROM_READ")
    ap.add_argument("--baud", type=int, default=115200, help="any rate, the target measures it")
    ap.add_argument("--wait", type=float, metavar="SEC",
                    help="keep sending autobaud for SEC seconds (power-cycle the target meanwhile)")
    ap.add_argument("--enter", metavar="LINE",
                    help="text command that makes the running application jump to the bootloader, "
                         "e.g. 'set.1 system.boot=1' (Forge firmware: software reset into the "
                         "bootloader about 1 s after the answer); sent at --app-baud first")
    ap.add_argument("--keep-identity", action="store_true",
                    help="after write-flash: set the identity found by --expect-device in the new "
                         "firmware (port firmware only, builds that serve several identities)")
    ap.add_argument("--expect-device", metavar="MODEL-PCB[,...]",
                    help="before anything else ask the running firmware (get.1 system.key.info) and "
                         "abort unless it reports this model and PCB, e.g. 96ELN3-4.1")
    ap.add_argument("--expect-clock", choices=("si5338", "none"),
                    help="before flashing check the board generation by the Si5338 clock generator on "
                         "I2C bus 1 (V40: si5338, V30: none); V30 and V40 96ELN report the same identity")
    ap.add_argument("--app-baud", type=int, default=38400, help="baud rate of the application")
    ap.add_argument("--resume", action="store_true",
                    help="bootloader still active from an earlier --stay: skip autobaud")
    ap.add_argument("--timeout", type=float, default=3.0)
    ap.add_argument("--stay", action="store_true",
                    help="do not send 'start application' at the end")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info", help="bootloader and firmware version")
    sub.add_parser("start", help="leave the bootloader, start the application")
    sub.add_parser("identify", help="MODEL-PCB of the running firmware (system.key.info)")
    te = sub.add_parser("export-tuning", help="loaded preset + device settings into a GUI .cfg (read only)")
    te.add_argument("out", help=".cfg file")
    tc = sub.add_parser("compare-tuning", help="compare the device with a .cfg (read only)")
    tc.add_argument("cfg")
    tr = sub.add_parser("restore-tuning", help="write a .cfg with set commands (needs --allow-write)")
    tr.add_argument("cfg")
    tr.add_argument("--allow-write", action="store_true")
    bk = sub.add_parser("backup", help="versions + flash + EEPROM into DIR (one session)")
    bk.add_argument("dir")
    for what in ("flash", "eeprom"):
        r = sub.add_parser(f"read-{what}", help=f"dump {what} to OUT.bin and OUT.hex")
        r.add_argument("out", help="output path without extension")
        r.add_argument("--start", type=lambda s: int(s, 0), default=0)
        r.add_argument("--size", type=lambda s: int(s, 0), default=None)
        w = sub.add_parser(f"write-{what}", help=f"program {what} (needs --allow-write)")
        w.add_argument("image", help=".hex or .bin")
        w.add_argument("--start", type=lambda s: int(s, 0), default=0)
        w.add_argument("--allow-write", action="store_true")
        w.add_argument("--no-verify", dest="verify", action="store_false",
                       help="skip read-back after writing")
        if what == "flash":
            w.add_argument("--keep-tuning", metavar="DIR",
                           help="export the tuning before flashing into DIR, compare it after the "
                                "start and report differences (needs the running firmware)")
            w.add_argument("--restore-tuning", action="store_true",
                           help="with --keep-tuning: write differences back after the start")
    args = ap.parse_args(argv)

    port = open_port(args)
    if args.cmd == "identify":  # running firmware only, no bootloader
        try:
            got = parse_key_info(app_query(port, args, "get.1 system.key.info"))
        finally:
            port.close()
        if not got:
            raise SystemExit("identify: no answer to get.1 system.key.info")
        print("-".join(got))
        return
    if args.cmd in ("export-tuning", "compare-tuning", "restore-tuning"):
        try:
            cmd_tuning(port, args)
        finally:
            port.close()
        return
    tuning_before = None
    if args.cmd == "write-flash" and args.keep_tuning and not args.resume:
        tuning_before = save_tuning(port, args, "before")
    bl = Bootloader(port, timeout=args.timeout, log=log)
    try:
        if args.expect_device and not args.resume:
            check_device(port, args)
        if getattr(args, "expect_clock", None) and not args.resume:
            check_clock(port, args)
        if args.enter and not args.resume:
            enter_bootloader(port, args)
        if args.resume:
            ok = bl.resume()
        else:
            ok = bl.connect(wait=args.wait, reopen=reopener(args)) if args.wait else bl.connect()
        if not ok:
            raise SystemExit("bootloader not reached (power-cycle the target while --wait runs; "
                             "after --stay use --resume)")
        log("connected")
        if args.cmd == "info":
            cmd_info(bl, args)
        elif args.cmd in ("read-flash", "read-eeprom"):
            cmd_read(bl, args, args.cmd.split("-")[1])
        elif args.cmd == "backup":
            cmd_backup(bl, args)
        elif args.cmd in ("write-flash", "write-eeprom"):
            cmd_write(bl, args, args.cmd.split("-")[1])
        if args.cmd == "start" or not args.stay:
            bl.start_application()
            log("application started")
            if args.cmd == "write-flash" and args.keep_identity:
                restore_identity(bl.port, args)
            if tuning_before is not None:
                check_tuning(bl.port, args, tuning_before)
    except BootloaderError as e:
        raise SystemExit(f"error: {e}")
    finally:
        bl.port.close()


if __name__ == "__main__":
    main()
