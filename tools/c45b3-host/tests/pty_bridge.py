#!/usr/bin/env python3
"""Serial device emulation for end-to-end tests without hardware.

Creates a pty (symlink LINK), emulates the running application until it
receives ENTER (own firmware: ">system.bootloader=1"), answers like the
firmware and then starts the native bootloader harness (= software reset into
chip45boot3). After "start application" the harness exits and the
application emulation runs again.

  pty_bridge.py LINK HARNESS FLASH_IN EEPROM_IN FLASH_OUT EEPROM_OUT
"""

import os
import pty
import select
import subprocess
import sys
import termios
import tty

ENTER = b">system.bootloader=1\r\n"
KEY_QUERY = b"<system.key.info\r\n"
# what the emulated firmware reports, e.g. FAG-96ELN3-4.1-0000-CCBYNCSA-20241206-MDD
KEY_INFO = os.environ.get("PTY_KEY_INFO", "FAG-96ELN3-4.1-0000-CCBYNCSA-20241206-MDD").encode()


def main():
    link, harness, fin, ein, fout, eout = sys.argv[1:7]
    master, slave = pty.openpty()
    tty.setraw(slave)
    attrs = termios.tcgetattr(slave)
    attrs[3] &= ~termios.ECHO
    termios.tcsetattr(slave, termios.TCSANOW, attrs)
    if os.path.lexists(link):
        os.unlink(link)
    os.symlink(os.ttyname(slave), link)
    print("pty", os.ttyname(slave), "->", link, flush=True)
    proc = None
    app_buf = b""
    try:
        while True:
            fds = [master] + ([proc.stdout] if proc else [])
            r, _, _ = select.select(fds, [], [], 0.05)
            if master in r:
                try:
                    data = os.read(master, 4096)
                except OSError:
                    data = b""
                if proc:
                    try:
                        proc.stdin.write(data)
                        proc.stdin.flush()
                    except BrokenPipeError:
                        pass
                else:
                    app_buf = (app_buf + data)[-256:]
                    if KEY_QUERY in app_buf:
                        app_buf = b""
                        os.write(master, b' <.2 system.key.info="' + KEY_INFO + b'"\r\n')
                    if ENTER in app_buf:
                        app_buf = b""
                        os.write(master, b" *.2 system.bootloader ok\r\n")
                        print("app: reset into bootloader", flush=True)
                        proc = subprocess.Popen([harness, fin, ein, fout, eout],
                                                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                                bufsize=0)
                        # the harness continues from the written image next time
                        fin = fout
            if proc and proc.stdout in r:
                out = os.read(proc.stdout.fileno(), 4096)
                if out:
                    os.write(master, out)
            if proc and proc.poll() is not None:
                rest = proc.stdout.read()
                if rest:
                    os.write(master, rest)
                print("bootloader left, application runs (exit %d)" % proc.returncode, flush=True)
                proc = None
    except KeyboardInterrupt:
        pass
    finally:
        if proc:
            proc.kill()
        os.unlink(link)


if __name__ == "__main__":
    main()
