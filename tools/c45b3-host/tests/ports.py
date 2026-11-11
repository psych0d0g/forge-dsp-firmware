"""Test ports: native C target (original chip45boot3 sources) over pipes,
and a wrapper that corrupts selected frames."""

import os
import select
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def ctarget_path(variant="full"):
    d = os.environ.get("C45B3_CTARGET_DIR", os.path.join(HERE, "ctarget", "build"))
    p = os.path.join(d, f"ctarget-{variant}")
    return p if os.access(p, os.X_OK) else None


class PipePort:
    def __init__(self, exe, flash=None, eeprom=None):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.tmp.name
        fin = ein = "-"
        if flash is not None:
            fin = os.path.join(t, "flash.in")
            open(fin, "wb").write(flash)
        if eeprom is not None:
            ein = os.path.join(t, "eeprom.in")
            open(ein, "wb").write(eeprom)
        self.fout = os.path.join(t, "flash.out")
        self.eout = os.path.join(t, "eeprom.out")
        self.proc = subprocess.Popen([exe, fin, ein, self.fout, self.eout],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, bufsize=0)
        self.fd = self.proc.stdout.fileno()
        self.stderr = ""
        self.flash = self.eeprom = None

    def write(self, data):
        self.proc.stdin.write(bytes(data))
        return len(data)

    def read(self, n=1):
        r, _, _ = select.select([self.fd], [], [], 0.02)
        if not r:
            return b""
        return os.read(self.fd, n)

    def reset_input_buffer(self):
        while select.select([self.fd], [], [], 0)[0]:
            if not os.read(self.fd, 4096):
                break

    def close(self):
        if self.proc.stdin and not self.proc.stdin.closed:
            try:
                self.proc.stdin.close()
            except BrokenPipeError:
                pass
        self.proc.stdout.read()
        self.stderr = self.proc.stderr.read().decode()
        self.proc.wait(timeout=10)
        if os.path.exists(self.fout):
            self.flash = open(self.fout, "rb").read()
            self.eeprom = open(self.eout, "rb").read()
        self.tmp.cleanup()


def _flip(c):
    for m in (0x40, 0x20, 0x10):
        if c ^ m not in (0x02, 0x03, 0x1B):
            return c ^ m
    raise AssertionError


class CorruptingPort:
    """Flips one byte in the n-th written frame (direction host->target) or
    in the n-th received frame (target->host)."""

    def __init__(self, inner, tx_frame=None, rx_frame=None):
        self.inner = inner
        self.tx_frame = tx_frame
        self.rx_frame = rx_frame
        self.tx_count = 0
        self.rx_count = 0
        self.rx_pos = 0

    def write(self, data):
        data = bytearray(data)
        if data[:1] == b"\x02":
            self.tx_count += 1
            if self.tx_count == self.tx_frame:
                data[2] = _flip(data[2])
        return self.inner.write(bytes(data))

    def read(self, n=1):
        d = bytearray(self.inner.read(n))
        for i, c in enumerate(d):
            if c == 0x02:
                self.rx_count += 1
                self.rx_pos = 0
            else:
                self.rx_pos += 1
                if self.rx_count == self.rx_frame and self.rx_pos == 2 and c not in (2, 3, 0x1B):
                    d[i] = _flip(c)
        return bytes(d)

    def reset_input_buffer(self):
        self.inner.reset_input_buffer()

    def close(self):
        self.inner.close()
