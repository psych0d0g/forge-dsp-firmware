#!/usr/bin/env python3
"""Measurement helper for the Forge DSP (issue #49): play a test signal into the Forge, record an
output on the PC line-in, send commands to the Forge at given times, analyse the recording.

Set-up and pitfalls: docs/measurement.md. Needs numpy (and scipy for 'clicks'), PipeWire tools
(pw-play, pw-metadata, pactl) and alsa-utils (arecord, amixer).

Signals
    forge_measure.py tone OUT.wav --freq 1000 --db -20 --seconds 30 [--rate 48000]
    forge_measure.py multitone OUT.wav --db -32 --seconds 40 [--count 24] [--avoid 160]
        (writes OUT.wav and OUT.tones.txt with the frequencies)

Run (playback + recording + timed Forge commands)
    forge_measure.py run --play SIG.wav --out REC.wav --seconds 12 \\
        --sink alsa_output.usb-..pro-output-0 --card 1 --source alsa_input.pci-..analog-stereo \\
        [--gain 0|30] [--rate 48000] \\
        [--port /dev/ttyUSB0] [--at 2 "set.1 gain.out.4=199"] [--at 7 "set.1 gain.out.4=223"]
    Commands with the same time go out together; set commands get their checksum.

Analysis
    forge_measure.py level REC.wav --freq 1000          level of the tone, RMS, strongest lines
    forge_measure.py response A.wav B.wav --tones SIG.tones.txt
                                                       level of every tone in B relative to A
    forge_measure.py clicks REC.wav --events 2,7 [--quiet 0.3,1.9]
                                                       dropouts (2 ms windows 12 dB below the level)
                                                       and spikes above 5 kHz higher than the
                                                       maximum of the quiet span + 3 dB
License: MIT
"""
import argparse
import os
import subprocess
import sys
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
C45B3 = os.path.join(HERE, "..", "c45b3-host")


def need_numpy():
    try:
        import numpy  # noqa: F401
    except ImportError:
        raise SystemExit("numpy is needed (pip install numpy scipy)")


# --- signals --------------------------------------------------------------------------------

def write_wav(path, data, rate):
    import numpy as np
    d = (np.clip(data, -1, 1) * (2 ** 31 - 1)).astype("<i4")
    if d.ndim == 1:
        d = np.stack([d, d], 1)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(4)
        w.setframerate(rate)
        w.writeframes(d.tobytes())


def tone(freq, db, seconds, rate):
    import numpy as np
    t = np.arange(int(rate * seconds)) / rate
    s = 10 ** (db / 20) * np.sin(2 * np.pi * freq * t)
    ramp = min(len(s) // 2, rate // 50)
    s[:ramp] *= np.linspace(0, 1, ramp)
    s[-ramp:] *= np.linspace(1, 0, ramp)
    return s


def multitone_freqs(count, lo=50.0, hi=16000.0, avoid=160.0):
    """Log spaced, moved off multiples of `avoid` (hum lines of the set-up) by 40 Hz."""
    import numpy as np
    fr = np.geomspace(lo, hi, count)
    if avoid:
        fr = np.array([f + (40 if abs(f / avoid - round(f / avoid)) * avoid < 25 else 0) for f in fr])
    return np.round(fr * 4) / 4


def multitone(freqs, db, seconds, rate, seed=2):
    import numpy as np
    t = np.arange(int(rate * seconds)) / rate
    rng = np.random.default_rng(seed)
    return sum(np.sin(2 * np.pi * f * t + rng.uniform(0, 2 * np.pi)) for f in freqs) * 10 ** (db / 20)


# --- recording ------------------------------------------------------------------------------

def read_wav(path):
    import numpy as np
    with wave.open(path) as w:
        rate = w.getframerate()
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i4").reshape(-1, w.getnchannels())
    return x[:, 0] / 2 ** 31, rate


def read_wav2(path):
    """both channels"""
    import numpy as np
    with wave.open(path) as w:
        rate = w.getframerate()
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i4").reshape(-1, w.getnchannels())
    return x / 2 ** 31, rate


def capture_cmd(card, out, seconds, gain, source=None):
    """arecord on the line-in. PipeWire restores its own gain (up to +50 dB) whenever it opens the
    device, so its source is suspended first (source: PipeWire node name) and the hardware gain set
    afterwards. gain: 0 or 30 dB hardware capture gain."""
    pre = [f"pactl suspend-source {source} 1"] if source else []
    pre.append(f"amixer -c {card} sset 'Capture',0 {63 if gain >= 30 else 23} cap >/dev/null")
    pre.append(f"amixer -c {card} sset 'Line Boost',0 0 >/dev/null")
    pre.append(f"amixer -c {card} sset 'Input Source',0 Line >/dev/null")
    return " ; ".join(pre) + f" ; arecord -q -D hw:{card},0 -f S32_LE -r 96000 -c 2 -d {int(seconds)} {out}"


class Forge:
    def __init__(self, port):
        sys.path.insert(0, C45B3)
        import serial
        from c45b3.tuning import App, checksum
        self.checksum = checksum
        self.app = App(serial.Serial(port, 38400, timeout=0.1), log=lambda *a: None)

    def send(self, line):
        if line.startswith("set.1 ") and ";" not in line:
            line += ";" + self.checksum(line[6:])
        return list(self.app.query(line, quiet=0.4, limit=10))


def run(args):
    sinks = args.sink
    force = ["pw-metadata", "-n", "settings", "0", "clock.force-rate", str(args.rate)]
    subprocess.run(force, check=False, stdout=subprocess.DEVNULL)
    forge = Forge(args.port) if args.at else None
    play = subprocess.Popen(["pw-play", "--target", sinks, "--rate", str(args.rate), "--channels", "2", args.play])
    try:
        time.sleep(args.lead)
        rec = subprocess.Popen(capture_cmd(args.card, args.out, args.seconds, args.gain, args.source), shell=True)
        t0 = time.monotonic()
        events = {}
        for t, line in args.at or []:
            events.setdefault(float(t), []).append(line)
        for t in sorted(events):
            delay = t - (time.monotonic() - t0)
            if delay > 0:
                time.sleep(delay)
            for line in events[t]:
                ans = forge.send(line)
                print(f"{time.monotonic() - t0:6.2f} s  {line}  ->  {' | '.join(a.strip() for a in ans)}")
        rec.wait()
    finally:
        play.terminate()
        play.wait()
        subprocess.run(["pw-metadata", "-n", "settings", "0", "clock.force-rate", "0"],
                       check=False, stdout=subprocess.DEVNULL)
        if args.source:
            subprocess.run(["pactl", "suspend-source", args.source, "0"], check=False)


# --- analysis -------------------------------------------------------------------------------

def spectrum(x, rate, skip=1.0):
    import numpy as np
    x = x[int(skip * rate):]
    x = x - x.mean()
    m = np.abs(np.fft.rfft(x * np.hanning(len(x)))) / len(x) * 4
    return np.fft.rfftfreq(len(x), 1 / rate), m


def line_level(f, m, freq):
    import numpy as np
    i = int(np.argmin(abs(f - freq)))
    return 20 * np.log10(m[max(i - 3, 0):i + 4].max() + 1e-15)


def level(path, freq):
    import numpy as np
    x, rate = read_wav(path)
    f, m = spectrum(x, rate)
    rms = 20 * np.log10(np.sqrt(np.mean((x[rate:] - x[rate:].mean()) ** 2)) + 1e-15)
    sel = f > 20
    peaks = [i for i in np.where(sel)[0][1:-1] if m[i] > m[i - 1] and m[i] >= m[i + 1]]
    peaks = sorted(peaks, key=lambda i: -m[i])[:5]
    return {"rms_dbfs": rms, "tone_dbfs": line_level(f, m, freq) if freq else None,
            "lines": [(round(float(f[i]), 1), round(float(20 * np.log10(m[i])), 1)) for i in peaks]}


def response(path_a, path_b, freqs):
    xa, ra = read_wav(path_a)
    xb, rb = read_wav(path_b)
    fa, ma = spectrum(xa, ra)
    fb, mb = spectrum(xb, rb)
    return [(f, line_level(fb, mb, f) - line_level(fa, ma, f)) for f in freqs]


def phase_lr(path, freqs, skip=1.0):
    """phase left minus right (degrees) at each tone: the same signal on two outputs, one with the
    filter, so the start time of the recording does not matter"""
    import numpy as np
    x, rate = read_wav2(path)
    x = x[int(skip * rate):]
    w = np.hanning(len(x))
    fl = np.fft.rfft((x[:, 0] - x[:, 0].mean()) * w)
    fr = np.fft.rfft((x[:, 1] - x[:, 1].mean()) * w)
    bins = np.fft.rfftfreq(len(x), 1 / rate)
    out = []
    for f in freqs:
        i = int(np.argmin(abs(bins - f)))
        i = i - 2 + int(np.argmax(abs(fl[i - 2:i + 3])))
        out.append(float(np.degrees(np.angle(fl[i] / fr[i]))))
    return out


def phase(path_a, path_b, freqs):
    """phase change of (left - right) from recording A to B, wrapped to -180..180"""
    a, b = phase_lr(path_a, freqs), phase_lr(path_b, freqs)
    return [(f, (pb - pa + 180) % 360 - 180) for f, pa, pb in zip(freqs, a, b)]


def clicks(path, events, quiet=(0.3, 1.9), window=2.5):
    """-> dict: level, dropouts [(t, ms)], per event window: ms above threshold and max dB."""
    import numpy as np
    from scipy.signal import butter, sosfiltfilt
    x, rate = read_wav(path)
    x = x - x.mean()
    win = int(0.002 * rate)
    n = len(x) // win
    env = 20 * np.log10(np.sqrt(np.mean(x[:n * win].reshape(n, win) ** 2, 1)) + 1e-12)
    q0, q1 = int(quiet[0] / 0.002), int(quiet[1] / 0.002)
    ref = float(np.median(env[q0:q1]))
    low = env < ref - 12
    drops, i = [], q0
    while i < n:
        if low[i]:
            j = i
            while j < n and low[j]:
                j += 1
            if j - i >= 2:
                drops.append((round(i * 0.002, 3), (j - i) * 2))
            i = j
        else:
            i += 1
    h = sosfiltfilt(butter(6, 5000, "hp", fs=rate, output="sos"), x)
    hw = int(0.001 * rate)
    m = len(h) // hw
    pk = np.max(np.abs(h[:m * hw].reshape(m, hw)), 1)
    t = np.arange(m) * 0.001
    base = pk[(t >= quiet[0]) & (t < quiet[1])].max()
    thr = base * 10 ** (3 / 20)
    res = []
    for e in events:
        sel = (t >= e - 0.1) & (t < e + window)
        res.append((e, int((pk[sel] > thr).sum()), float(20 * np.log10(pk[sel].max() / base))))
    return {"level_dbfs": ref, "dropouts": drops, "events": res}


# --- command line ---------------------------------------------------------------------------

def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("tone")
    p.add_argument("out")
    p.add_argument("--freq", type=float, default=1000)
    p.add_argument("--db", type=float, default=-20)
    p.add_argument("--seconds", type=float, default=30)
    p.add_argument("--rate", type=int, default=48000)
    p = sub.add_parser("multitone")
    p.add_argument("out")
    p.add_argument("--db", type=float, default=-32)
    p.add_argument("--seconds", type=float, default=40)
    p.add_argument("--count", type=int, default=24)
    p.add_argument("--avoid", type=float, default=160)
    p.add_argument("--rate", type=int, default=48000)
    p = sub.add_parser("run")
    p.add_argument("--play", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--seconds", type=float, default=12)
    p.add_argument("--sink", required=True, help="PipeWire node of the S/PDIF card")
    p.add_argument("--card", type=int, default=1, help="ALSA card of the line-in")
    p.add_argument("--source", help="PipeWire node of the line-in, suspended while recording")
    p.add_argument("--gain", type=int, default=0, choices=(0, 30))
    p.add_argument("--rate", type=int, default=48000)
    p.add_argument("--lead", type=float, default=2.0, help="seconds of playback before recording")
    p.add_argument("--port", default="/dev/ttyUSB0")
    p.add_argument("--at", nargs=2, action="append", metavar=("T", "CMD"))
    p = sub.add_parser("level")
    p.add_argument("wav")
    p.add_argument("--freq", type=float, default=1000)
    p = sub.add_parser("response")
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--tones", required=True)
    p = sub.add_parser("phase")
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--tones", required=True)
    p = sub.add_parser("clicks")
    p.add_argument("wav")
    p.add_argument("--events", default="")
    p.add_argument("--quiet", default="0.3,1.9")
    a = ap.parse_args(argv)
    need_numpy()
    if a.cmd == "tone":
        write_wav(a.out, tone(a.freq, a.db, a.seconds, a.rate), a.rate)
    elif a.cmd == "multitone":
        fr = multitone_freqs(a.count, avoid=a.avoid)
        write_wav(a.out, multitone(fr, a.db, a.seconds, a.rate), a.rate)
        with open(os.path.splitext(a.out)[0] + ".tones.txt", "w") as fh:
            fh.write("\n".join(f"{f:g}" for f in fr) + "\n")
    elif a.cmd == "run":
        run(a)
    elif a.cmd == "level":
        r = level(a.wav, a.freq)
        print(f"RMS {r['rms_dbfs']:.1f} dBFS, {a.freq:g} Hz {r['tone_dbfs']:.1f} dBFS, lines {r['lines']}")
    elif a.cmd == "response":
        freqs = [float(x) for x in open(a.tones).read().split()]
        for f, d in response(a.a, a.b, freqs):
            print(f"{f:8.1f} Hz  {d:+6.2f} dB")
    elif a.cmd == "phase":
        freqs = [float(x) for x in open(a.tones).read().split()]
        for f, d in phase(a.a, a.b, freqs):
            print(f"{f:8.1f} Hz  {d:+7.1f} deg")
    elif a.cmd == "clicks":
        ev = [float(x) for x in a.events.split(",") if x]
        q = tuple(float(x) for x in a.quiet.split(","))
        r = clicks(a.wav, ev, q)
        print(f"level {r['level_dbfs']:.1f} dBFS, dropouts {r['dropouts'][:8] or 'none'}")
        for e, nms, mx in r["events"]:
            print(f"  event {e:g} s: {nms} ms above quiet max +3 dB (max {mx:+.1f} dB)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
