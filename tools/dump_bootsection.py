#!/usr/bin/env python3
"""Read the boot section (bootloader, 8 KiB at 0x40000) through the running port firmware.

Uses the read-only port extension `get.1 system.hardware.boot.N` (N 1..128, 64 bytes each).
Writes <out>.bin and prints its sha256. If the lock bits forbid reading the boot section from
the application (BLBB = RWLOCK), the MCU returns no real data (the dump is uniform).

    python3 tools/dump_bootsection.py /dev/ttyUSB0 bootsection.bin
"""
import hashlib
import re
import sys
import time

import serial


def main():
    port, out = sys.argv[1], sys.argv[2]
    data = bytearray()
    with serial.Serial(port, 38400, timeout=1) as s:
        s.reset_input_buffer()
        for n in range(1, 129):
            s.write(f"get.1 system.hardware.boot.{n}\r\n".encode())
            deadline = time.time() + 2
            while True:
                line = s.readline().decode(errors="replace")
                m = re.search(rf'system\.hardware\.boot\.{n}="([0-9A-F]{{128}})"', line)
                if m:
                    data += bytes.fromhex(m.group(1))
                    break
                if "!." in line or time.time() > deadline:
                    sys.exit(f"block {n}: no answer ({line.strip()!r})")
    open(out, "wb").write(data)
    distinct = len(set(data))
    print(f"{out}: {len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()}, {distinct} distinct byte values")
    if distinct <= 1:
        print("uniform: the application probably cannot read the boot section (BLBB = RWLOCK)")


if __name__ == "__main__":
    main()
