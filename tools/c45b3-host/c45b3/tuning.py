"""Tuning of a Forge over the text protocol: export, compare, restore (issue #30).

The file format is the one the original GUI writes ("Konfiguration speichern"):

    [96ELN]
    FILE_v10 = "gain.out.1=200"
    FILE_v10 = "biquad.1.1.calc=[1,1000,35,1414,0,128]"

The GUI loads such a file line by line as set commands; the port firmware loads it from the
microSD card the same way (#24). The export covers the loaded preset (gains, mutes, phase,
delays, mixer, biquads) and the device settings (channel names, IR codes, MODC blocks), i.e.
what the original GUI saves plus the MODC configuration. Works with the original firmware and
the port: only documented read commands are used.
"""
import re
import time

LINE_LABEL = "FILE_v10"

# read commands and whether a device may lack them (answered with an error)
QUERIES = [
    ("mixer.position", False),
    ("mixer.*", False),
    ("gain.in.*", False),
    ("gain.spdif.*", False),
    ("gain.out.*", False),
    ("mute.out.*", False),
    ("phase.out.*", False),
    ("delay.*", False),
    ("delay.in.*", True),       # only 96ELN builds
    ("system.channel.name.*", False),
    ("extender.ir.custom.*", True),
    ("modc.*", False),
    ("biquad.*", False),
]

ANSWER = re.compile(r'^\s*<\.\d+ (\S+?)=(.*?)\s*$')


class App:
    """Line-based access to the running application."""

    def __init__(self, port, baud=38400, log=print):
        self.port, self.baud, self.log = port, baud, log

    def _baud(self):
        try:
            self.port.baudrate = self.baud
        except AttributeError:
            pass

    def query(self, line, quiet=0.4, limit=30.0):
        """Send one line, collect answer lines until the device is quiet for `quiet` s."""
        self._baud()
        self.port.reset_input_buffer()
        self.port.write(line.encode("latin-1") + b"\r\n")
        buf = bytearray()
        t_last = time.monotonic()
        t_end = t_last + limit
        while time.monotonic() < t_end:
            chunk = self.port.read(256)
            if chunk:
                buf += chunk
                t_last = time.monotonic()
            elif time.monotonic() - t_last > quiet and buf:
                break
            elif time.monotonic() - t_last > max(quiet, 2.0):
                break
        return [l for l in buf.decode("latin-1").replace("\r", "").split("\n") if l.strip()]

    def ready(self, timeout=90.0):
        """Answers twice in a row (the first ping can come before the preset load blocks it)."""
        t_end = time.monotonic() + timeout
        ok = 0
        while time.monotonic() < t_end:
            if any("pong" in l for l in self.query("get.1 system.ping", quiet=0.3, limit=2)):
                ok += 1
                if ok >= 2:
                    return True
            else:
                ok = 0
            time.sleep(2)
        return False


def checksum(body):
    return "%02X" % (sum(body.encode("latin-1")) & 0xFF)


def model_of(app):
    for l in app.query("get.1 system.key.info.type", quiet=0.3, limit=3):
        m = re.search(r'system\.key\.info\.type="([^"]*)"', l)
        if m:
            return m.group(1)
    raise SystemExit("tuning: no answer to get.1 system.key.info.type")


def blank(reg, value):
    """Entries the firmware reports for never written storage (all bytes 0xFF)."""
    if reg.startswith("biquad."):
        return value.startswith("[255,")
    if reg.startswith("modc."):
        return False
    if re.fullmatch(r"delay\.in\.[78]", reg):
        return True  # digital inputs: get lists them, they have no delay (set refuses them)
    return value in ("255", "32767", "65535", '""')


def export(app):
    """-> (model, [(register, value)]) of the loaded preset and the device settings."""
    model = model_of(app)
    out = []
    for q, optional in QUERIES:
        lines = app.query("get.1 " + q, quiet=0.5, limit=60)
        if any(l.lstrip().startswith("!.") for l in lines):
            if optional:
                continue
            raise SystemExit(f"tuning: {q}: {lines[0].strip()}")
        for l in lines:
            m = ANSWER.match(l)
            if m and m.group(1) not in ("biquad", "modc.enable"):
                out.append((m.group(1), m.group(2)))
    return model, out


def to_set(reg, value):
    """Register as read -> register as written (stored biquad values are recomputed)."""
    if re.fullmatch(r"biquad\.\d+\.\d+", reg):
        return reg + ".calc", value
    return reg, value


def to_cfg(model, entries, comment=()):
    lines = [f"; {c}" for c in comment] + [f"[{model}]"]
    for reg, value in entries:
        if blank(reg, value):
            continue
        r, v = to_set(reg, value)
        lines.append(f'{LINE_LABEL} = "{r}={v}"')
    return "\n".join(lines) + "\n"


def parse_cfg(text):
    """-> (model, [(register, value)]) of a GUI .cfg file."""
    model, entries = None, []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith((";", "#")):
            continue
        if line.startswith("[") and line.endswith("]"):
            if model is None:
                model = line[1:-1]
            continue
        m = re.match(r'^[^=]+?\s*=\s*"(.*)"$', line)
        if not m or "=" not in m.group(1):
            continue
        reg, value = m.group(1).split("=", 1)
        entries.append((reg.strip(), value.strip()))
    return model, entries


def canonical(reg, value):
    """Equal filters compare equal: a biquad without the active flag (bit 7) or of type 0 is a
    bypass whatever f/gain/q/b say (the firmware clamps f >= 10 and q >= 1 when it is written,
    so the factory zeros [0,0,0,0,0,0] cannot be written back literally)."""
    if reg.startswith("biquad."):
        m = re.fullmatch(r"\[(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+)(?:,(-?\d+))?\]", value)
        if m:
            t = int(m.group(1))
            flags = int(m.group(6)) if m.group(6) is not None else 128
            if t == 0 or not flags & 0x80:
                return "bypass"
    return value


def normalize(entries):
    """dict register -> value in the set form, blanks dropped (for comparing)."""
    d = {}
    for reg, value in entries:
        if blank(reg, value):
            continue
        r, v = to_set(reg, value)
        d[r] = canonical(r, v)
    return d


def compare(a, b):
    """-> list of (register, value in a, value in b) that differ."""
    da, db = normalize(a), normalize(b)
    return [(k, da.get(k), db.get(k)) for k in sorted(set(da) | set(db), key=_order) if da.get(k) != db.get(k)]


def _order(reg):
    return [(0, int(p), "") if p.isdigit() else (1, 0, p) for p in reg.split(".")]


def restore(app, model, entries, log=print):
    """Write the values of the file that differ on the device (set commands with checksum).
    -> (written, errors)."""
    have_model, have = export(app)
    if have_model != model:
        raise SystemExit(f"tuning: file is for {model}, the device is {have_model}; not written")
    want = {}
    for reg, value in entries:
        if not blank(reg, value):
            r, v = to_set(reg, value)
            want[r] = v
    diff = compare(entries, have)
    written = errors = 0
    for reg, a, b in diff:
        if a is None:
            continue  # only on the device: nothing to write
        body = f"{reg}={want[reg]}"
        ans = app.query(f"set.1 {body};{checksum(body)}", quiet=0.15, limit=5)
        written += 1
        if not any(reg in l and (" ok" in l or "<." in l) for l in ans):
            errors += 1
            log(f"tuning: {body}: {' '.join(x.strip() for x in ans) or 'no answer'}")
    return written, errors
