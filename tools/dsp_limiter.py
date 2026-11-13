#!/usr/bin/env python3
"""Output limiter for DSP program 2 (cores 2/3, the outputs), issue #58.

Per output word W (the value DO1..DO4 send in the next frame) with its gain g (coefficient
memory, written by the DSP like the smoothed output gains):

    y  = W * g                      soft part: g falls fast while over the threshold (attack),
    y1 = y / T                      returns slowly to 1 below it (release)
    yc = clip32(y1)                 hard part: CLP32 clips at full scale, i.e. y at T exactly,
    W  = yc * T                     sign kept, no division
    over = |y1| - |yc| > eps        "CLP32 had to clip"; COMP with operands of opposite sign
    g  = over ? g * att : g * (1 - rel) + rel

Threshold T as a coefficient (5.23): 1/T up to 16, i.e. down to -24 dBFS. Off: 1/T = T = 1 and
att = 1, then g stays exactly 1 and every DO value is bit identical to the program without the
block (CLP32 at the output clips the same). The block runs after the last output word is written
and before STOP, so the latency does not change.

I2C (written by the MCU, kind 1 = coefficients), five free subaddresses from 0xF0 (subs_of):
    subs[k] (k = DO slot 0..3): [1/T, T] of that output
    subs[4]:                    [att, 1 - rel, rel] for all outputs
Not reachable over I2C: g (state, starts at 1.0), eps.

    dsp_limiter.py check FLAVOUR_DATA.h     add the block to program 2 of a flavour, check rules
                                            and transparency in the simulator
License: MIT
"""
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dsp_image  # noqa: E402
import tas3108_asm as asm  # noqa: E402
import tas3108_dis as dis  # noqa: E402
import tas3108_sim as sim  # noqa: E402

COEF_BASE = 1001        # default start of the 16 coefficient words; add_limiter takes the first free run
N_COEF = 16
DATA_TEMPS = (0x1D6, 0x1D7)
SUB_MIN = 0xF0         # the five subaddresses: the first free (kind 0) from here
ONE = 1 << 23           # 1.0 in 5.23


def layout(base=COEF_BASE):
    """coefficient addresses: per output (1/T, T, g), then att, 1-rel, rel, eps"""
    COEF_BASE = base
    c = {}
    for k in range(4):
        c[f"it{k}"], c[f"t{k}"], c[f"g{k}"] = COEF_BASE + 3 * k, COEF_BASE + 3 * k + 1, COEF_BASE + 3 * k + 2
    c["att"], c["omr"], c["rel"], c["eps"] = COEF_BASE + 12, COEF_BASE + 13, COEF_BASE + 14, COEF_BASE + 15
    return c


def defaults(base=COEF_BASE):
    """off: thresholds at full scale, no attack; release 1/1024 per sample"""
    c, v = layout(base), {}
    rel = 1 << 13
    for k in range(4):
        v[c[f"it{k}"]] = v[c[f"t{k}"]] = v[c[f"g{k}"]] = ONE
    v[c["att"]], v[c["omr"]], v[c["rel"]], v[c["eps"]] = ONE, ONE - rel, rel, -1
    return v


def source(out_words, base=COEF_BASE, temps=DATA_TEMPS):
    """assembler text of the block for the output words of DO1..DO4"""
    c = layout(base)
    ta, tr = temps
    C = lambda a: f"C_{a:03X}"
    D = lambda a: f"D_{a:03X}"
    lines = []
    for k, w in enumerate(out_words):
        g, it, t = C(c[f"g{k}"]), C(c[f"it{k}"]), C(c[f"t{k}"])
        lines += [
            f"; limiter DO{k + 1} ({D(w)})",
            f"NOP | CLRACC | LD({D(w)},MD) | LD({g},MC) | NOP",              # MD = W, MC = g
            "NOP | NOP | NOP | NOP | NOP",                                       # MR = W * g
            f"NOP | ADD(ACC,MR,NONE,MD) | NOP | LD({it},MC) | NOP",            # MD = y, MC = 1/T
            "NOP | NOP | NOP | NOP | NOP",                                       # MR = y / T
            "NOP | ADD(ACC,MR,NONE,B) | NOP | NOP | NOP",                        # B = y1 (ACC = 0)
            "THRU(B) | NOP | NOP | NOP | NOP",                                   # BR = y1
            "ABS(B) | ADD(BR,ZERO,CLP32,B) | NOP | NOP | NOP",                   # BR = |y1|, B = yc
            f"THRU(B) | ADD(BR,ZERO,NONE,L) | NOP | LD({t},MC) | NOP",          # BR = yc, L = |y1|, MC = T
            "ABS(B) | ADD(BR,ZERO,NONE,MD) | NOP | NOP | NOP",                   # BR = |yc|, MD = yc
            "NOP | ADD(BR,ZERO,NONE,B) | NOP | NOP | NOP",                       # B = |yc|; MR = yc * T
            "NEG(B) | ADD(ACC,MR,NONE,B) | NOP | NOP | NOP",                     # BR = -|yc|, B = new W
            f"THRU(L) | NOP | NOP | LD({C(c['eps'])},L) | ST(B,DATA,{D(w)})",  # LR = |y1|, W stored
            "NOP | ADD(BR,LR,NONE,B) | NOP | NOP | NOP",                         # B = d = |y1| - |yc|
            "THRU(B) | NOP | NOP | NOP | NOP",                                   # BR = d
            f"THRU(L) | NOP | NOP | LD({g},L) | NOP",                            # LR = -eps, L = g
            f"THRU(L) | COMP(BR,LR) | NOP | LD({C(c['att'])},MC) | NOP",       # over = d > eps; LR = g
            f"NOP | ADD(ACC,LR,NONE,MD) | NOP | LD({C(c['rel'])},L) | NOP",    # MD = g (ACC = 0)
            "THRU(L) | NOP | NOP | NOP | NOP",                                   # LR = rel; MR = g * att
            f"NOP | ADD(ACC,MR,NONE,B) | NOP | LD({C(c['omr'])},MC) | NOP",    # B = g * att
            f"NOP | NOP | NOP | NOP | ST(B,DATA,{D(ta)})",                      # MR = g * (1 - rel)
            "NOP | ADD(ACC,MR,NONE,ACC) | NOP | NOP | NOP",
            "NOP | ADD(ACC,LR,NONE,B) | NOP | NOP | NOP",                        # B = g * (1 - rel) + rel
            f"NOP | NOP | NOP | NOP | ST(B,DATA,{D(tr)})",
            "NOP | NOP | NOP | NOP | NOP",                                       # no load right after the store
            f"NOP | NOP | LD({D(tr)},B) | NOP | NOP",
            f"NOP | NOP | LDC({D(ta)},B) | NOP | NOP",                           # over: the attack value
            f"NOP | NOP | NOP | NOP | ST(B,COEF,{g})",
        ]
    return "\n".join(lines)


def _last_stop(words):
    stops = [i for i, w in enumerate(words) if dis.fields(w)[0] == 0x15]
    if len(stops) < 2:
        raise SystemExit("no final STOP in program 2")
    return stops[-1]


def add_limiter(img, out_words):
    """program 2 image with the limiter block before the last STOP, its coefficients as an
    initialised block (off) and its subaddresses in the table -> image"""
    blocks, rest = dsp_image.parse(img)
    words = dis.program(blocks)
    used_coef, used_data = set(), set()
    for w in words:
        t = dis.disasm(w)[0]
        import re
        used_coef |= {int(a, 16) for a in re.findall(r"C_([0-9A-F]{3})", t)}
        used_data |= {int(a, 16) for a in re.findall(r"D_([0-9A-F]{3})", t)}
    regs = dsp_image.regmap(blocks)
    for s, a, n, k in regs:
        if k == 1:
            used_coef |= set(range(a, a + n // 4))
    for b in blocks:
        if b.mem == 3:
            used_coef |= set(range(b.addr, b.addr + len(b.words)))
        if b.mem == 4:
            used_data |= set(range(b.addr, b.addr + len(b.words)))
    base = next((a for a in range(0x3FE - N_COEF + 1) if not used_coef & set(range(a, a + N_COEF))), None)
    if base is None:
        raise SystemExit(f"no {N_COEF} free coefficient words for the limiter")
    c = layout(base)
    temps = [a for a in range(0x1C0, 0x3FE) if a not in used_data][:2]
    if len(temps) < 2:
        raise SystemExit("no free data words for the limiter")
    subs = [s for s, a, n, k in regs if k == 0 and SUB_MIN <= s < 0xFE][:5]
    if len(subs) < 5:
        raise SystemExit("no five free subaddresses for the limiter")
    new = asm.assemble(source(out_words, base, temps))
    i = _last_stop(words)
    prog = words[:i] + new + words[i:]
    # branch targets behind the insertion point move along (none in the Forge programs)
    for w in words[i:]:
        if dis.fields(w)[0] in (0x12, 0x13, 0x14) and dis.fields(w)[7] > i:
            raise SystemExit("branch behind the limiter block")
    out = []
    dv = defaults(base)
    coef_block = dsp_image.Block(3, base, b"".join((dv[a] & 0xFFFFFFFF).to_bytes(4, "little") for a in range(base, base + N_COEF)), 4)
    for b in asm.replace_program(blocks, prog):
        if b.mem == 0 and b.addr <= dsp_image.REG_TABLE < b.addr + len(b.data):
            d = bytearray(b.data)
            entries = [(c[f"it{k}"], 8) for k in range(4)] + [(c["att"], 12)]
            for sub, (a, n) in zip(subs, entries):
                o = dsp_image.REG_TABLE - b.addr + 4 * sub
                d[o:o + 4] = bytes([a >> 8, a & 0xFF, n, 1])
            b = dsp_image.Block(b.mem, b.addr, bytes(d), b.trailer_len)
        if b.terminator:
            out.append(coef_block)
        out.append(b)
    return dsp_image.build(out, rest)


def subs_of(img):
    """the limiter subaddresses of an image: the five 8/8/8/8/12-byte coefficient entries from 0xF0
    that point at a run of 16 words -> [sub per DO slot 0..3, shared] or None"""
    regs = dsp_image.regmap(dsp_image.parse(img)[0])
    cand = [(s, a, n) for s, a, n, k in regs if k == 1 and s >= SUB_MIN and n in (8, 12)]
    for i in range(len(cand) - 4):
        run = cand[i:i + 5]
        if [n for _, _, n in run] == [8, 8, 8, 8, 12] and all(run[j][1] == run[0][1] + 3 * j for j in range(4)) \
                and run[4][1] == run[0][1] + 12:
            return [s for s, _, _ in run]
    return None


def base_of(img):
    """start of the limiter coefficients of an image"""
    subs = subs_of(img)
    return dsp_image.regmap(dsp_image.parse(img)[0])[subs[0]][1] if subs else None


def run(img, frames, inputs, coef_override=None, seed=1, like=None):
    """DO1..DO8 per frame of a boot image in the simulator; inputs(n) -> 8 input ints.
    like: another image whose coefficient and data start values are used (initial_memory draws
    random register values in table order, so added subaddresses would shift them)"""
    blocks, _ = dsp_image.parse(img)
    coef, data = sim.initial_memory(dsp_image.parse(like)[0] if like else blocks, random.Random(seed))
    own, own_data = sim.initial_memory(blocks, random.Random(seed))
    base = base_of(img)
    if base is not None:
        for a in range(base, base + N_COEF):
            coef[a] = defaults(base)[a]
    data = {**own_data, **data}
    coef.update(coef_override or {})
    core = sim.Core(dis.program(blocks), coef, data)
    return [core.frame(inputs(n)) for n in range(frames)], core


def transparent(base, lim, frames=400, seed=3):
    """off: DO of both images equal for random inputs (also beyond full scale) -> None or text"""
    rng = random.Random(seed)
    ins = [[rng.randrange(-(1 << 23), 1 << 23) for _ in range(8)] for _ in range(frames)]
    a, _ = run(base, frames, lambda n: ins[n], seed=seed)
    b, _ = run(lim, frames, lambda n: ins[n], seed=seed, like=base)
    for n, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return f"sample {n}: {x} vs {y}"
    return None


def coefs(t_db, attack=0.5, release=1 / 1024):
    """coefficient values of one threshold (dBFS, 0 = off) and the shared attack/release factors
    -> ({it, t}, {att, omr, rel}) as 5.23 integers; off is exact: 1/T = T = 1, att = 1"""
    if t_db >= 0:
        return (ONE, ONE), (ONE, ONE - round(release * ONE), round(release * ONE))
    t = 10 ** (t_db / 20)
    rel = round(release * ONE)
    return (round(ONE / t), round(t * ONE)), (round(attack * ONE), ONE - rel, rel)


def block_core(out_word=0x100, t_db=-6.0, attack=0.5, release=1 / 1024):
    """only the limiter block of one output in the simulator (W set from outside)"""
    words = asm.assemble(source([out_word]) + "\nSTOP | NOP | NOP | NOP | PCADDR(0x000)")
    c = layout()
    coef = defaults()
    (it, t), (att, omr, rel) = coefs(t_db, attack, release)
    coef.update({c["it0"]: it, c["t0"]: t, c["att"]: att, c["omr"]: omr, c["rel"]: rel})
    return sim.Core(words, coef, {})


def main(argv):
    if len(argv) == 2 and argv[0] == "check":
        import dsp_opt_gen
        path = argv[1]
        base, _, _ = dsp_opt_gen.optimize(dsp_image.extract(path, 2), 2)
        outs = dsp_opt_gen.do_words(base)[:4]
        lim = add_limiter(base, outs)
        blocks, _ = dsp_image.parse(lim)
        words = dis.program(blocks)
        errs = asm.check(words)
        print("rules:", errs[:5] or "ok", "| cycles", asm.cycles(dis.program(dsp_image.parse(base)[0])), "->", asm.cycles(words))
        print("transparent when off:", transparent(base, lim) or "yes")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
