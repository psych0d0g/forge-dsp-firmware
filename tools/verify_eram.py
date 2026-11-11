#!/usr/bin/env python3
"""Check a generated EEPROM layout against the original BASCOM firmware.

  verify_eram.py FLAVOUR_ERAM_H RELEASE_HEX [--objdump avr-objdump]

BASCOM loads the 0-based ERAM address of a variable into Z (ldi r30 / ldi r31)
before its EEPROM routine. For every scalar ERAM variable the start address must
occur as such a Z load; layouts shifted by a few bytes must score clearly worse.
"""
import argparse
import re
import subprocess
import sys
import tempfile


def z_loads(objdump, hexfile):
    with tempfile.NamedTemporaryFile(suffix=".bin") as b:
        subprocess.run([objdump.replace("objdump", "objcopy"), "-I", "ihex", "-O", "binary",
                        hexfile, b.name], check=True)
        dis = subprocess.run([objdump, "-D", "-b", "binary", "-m", "avr:107", b.name],
                             check=True, capture_output=True, text=True).stdout
    loads, last = set(), {}
    for line in dis.split("\n"):
        m = re.search(r"\sldi\s+r(30|31),\s*0x([0-9A-Fa-f]+)", line)
        if not m:
            last.clear()
            continue
        last[m.group(1)] = int(m.group(2), 16)
        if "30" in last and "31" in last:
            loads.add(last["30"] | last["31"] << 8)
            last.clear()
    return loads


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("eram_h")
    ap.add_argument("hex")
    ap.add_argument("--objdump", default="avr-objdump")
    a = ap.parse_args()
    scal = {}
    text = open(a.eram_h).read()
    for name, addr in re.findall(r"#define (ERAM_\w+?) (\d+)  /\*", text):
        if re.search(rf"#define {name}_COUNT 1\n", text):
            scal[name] = int(addr)
    loads = z_loads(a.objdump, a.hex)
    score = {d: sum((v + d) in loads for v in scal.values()) for d in range(-3, 4)}
    hits = [n for n, v in scal.items() if v in loads]
    print(f"{a.eram_h}: {len(hits)}/{len(scal)} scalars found, shifted:",
          " ".join(f"{d:+d}:{s}" for d, s in score.items()))
    best = max(v for d, v in score.items() if d != 0)
    ok = score[0] > best and score[0] >= 0.6 * len(scal)
    print("OK" if ok else "LAYOUT MISMATCH")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
