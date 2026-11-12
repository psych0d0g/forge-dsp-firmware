#!/usr/bin/env python3
"""Program transformations for TAS3108 DSP programs (issue #47).

interleave_delays(words)
    The input section of the Forge programs handles every channel as
        issue:   CLRACC | LD(D_in,L) ; THRU(L) ; ADD(ACC,LR,NONE,DLYI) | DLYPTR(p)
        wait:    13 x NOP
        ST(DLYO,DATA,D_x)
        work:    volume block of this channel (may be empty)
        copy:    [NOP] LD(D_y,B) ; 4 x ST(B,DATA,D_y)   (stores the value it just loaded)
    New order: the delay of channel k+1 is issued directly after ST(DLYO) of channel k and the
    work of channel k runs in its wait time; ST(DLYO) of channel k+1 is merged into the first
    instruction of that work with a free store field at least 14 instructions after DLYPTR
    (NOPs are added if the work is shorter). The copy blocks are dropped. Delay operations
    still do not overlap (one DLYPTR at a time).

fill_waits_forward(words, regs)
    The output section of program 2 handles every channel as
        chain k | issue k | wait | ST(DLYO) k | volume k | chain k+1 | issue k+1 ...
    Chain k+1 does not depend on the delay or volume of channel k, so it runs in the wait time:
        chain k | issue k | chain k+1 (+ ST(DLYO) k merged) | volume k | issue k+1 ...
    A chain starts at the first instruction after ST(DLYO) that loads a biquad coefficient
    (5-word register of the subaddress table).

Every result must pass tas3108_asm.check and tas3108_sim.compare against the original.

Usage:
    tas3108_opt.py interleave IN.bin OUT.bin     program 1: delay waits filled with volume blocks
    tas3108_opt.py forward IN.bin OUT.bin        program 2: delay waits filled with the next chain
License: MIT
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsp_image  # noqa: E402
import tas3108_asm as asm  # noqa: E402
import tas3108_dis as dis  # noqa: E402

F = dis.fields


def _is_dlyptr(w):
    a1, _, _, _, _, _, m3, d3 = F(w)
    return a1 not in dis.BRANCH and m3 == 0 and d3 != 0


def _copy_block(seg):
    """Length of a trailing copy block (optional NOP, LD(X,B), ST(B,DATA,X) x n) in seg."""
    n = len(seg)
    i = n
    while i > 0:
        a1, a2, m1, d1, m2, d2, m3, d3 = F(seg[i - 1])
        if (a1, a2, m1, d1, m2, d2, m3) == (0, 0, 0, 0, 0, 0, 2):
            i -= 1
        else:
            break
    stores = n - i
    if stores == 0 or i == 0:
        return 0
    target = F(seg[n - 1])[7]
    if any(F(w)[7] != target for w in seg[i:]):
        return 0
    a1, a2, m1, d1, m2, d2, m3, d3 = F(seg[i - 1])
    if not ((a1, a2, m2, d2, m3, d3) == (0, 0, 0, 0, 0, 0) and m1 == 0x02 and d1 == target):
        return 0
    i -= 1
    if i > 0 and seg[i - 1] == 0:
        i -= 1
    return n - i


def interleave_delays(words):
    words = list(words)
    ptrs = [i for i, w in enumerate(words) if _is_dlyptr(w)]
    if not ptrs:
        return words
    chans = []
    for j, p in enumerate(ptrs):
        start = p - 2
        st = next(i for i in range(p + 1, len(words)) if F(words[i])[6] == 4)
        if any(words[i] for i in range(p + 1, st)):
            raise ValueError(f"0x{p:03X}: wait time is not all NOP")
        end = ptrs[j + 1] - 2 if j + 1 < len(ptrs) else None
        chans.append((start, p, st, end))
    # the last channel ends after its copy block (NOPs, LD(X,B), stores of B)
    last_start, last_p, last_st, _ = chans[-1]
    i = last_st + 1
    while i < len(words) and i - last_st < 12 and (words[i] == 0 or F(words[i])[2] == 0x02 or F(words[i])[6] == 2):
        i += 1
    chans[-1] = (last_start, last_p, last_st, i)

    out = words[:chans[0][0]]
    pending_st = None  # (ST(DLYO) word, index of DLYPTR in out)

    def place(work):
        nonlocal pending_st
        for w in work:
            out.append(w)
        if pending_st is not None:
            st_word, p_at = pending_st
            # merge into the first instruction >= p_at + 14 with a free store field
            for idx in range(p_at + asm.DLY_DISTANCE, len(out)):
                a1, a2, m1, d1, m2, d2, m3, d3 = F(out[idx])
                if a1 not in dis.BRANCH and m3 == 0 and d3 == 0:
                    out[idx] |= st_word
                    break
            else:
                while len(out) < p_at + asm.DLY_DISTANCE:
                    out.append(0)
                out.append(st_word)
            pending_st = None

    prev_work = []
    for start, p, st, end in chans:
        issue = words[start:p + 1]
        seg = words[st + 1:end]
        work = seg[:len(seg) - _copy_block(seg)]
        out.extend(issue)
        pending_st = (words[st], len(out) - 1)
        place(prev_work)
        prev_work = work
    place([])
    out.extend(prev_work)
    out.extend(words[chans[-1][3]:])
    return _fix_jumps(words, out, chans[0][0], chans[-1][3])


def _merge_st(out, st_word, p_at):
    """Merge ST(DLYO) into the first free store field >= DLYPTR + 14, else append NOPs."""
    for idx in range(p_at + asm.DLY_DISTANCE, len(out)):
        a1, a2, m1, d1, m2, d2, m3, d3 = F(out[idx])
        if a1 not in dis.BRANCH and m3 == 0 and d3 == 0:
            out[idx] |= st_word
            return
    while len(out) < p_at + asm.DLY_DISTANCE:
        out.append(0)
    out.append(st_word)


def fill_waits_forward(words, regs):
    words = list(words)
    bq = {a + k for s, a, n, kind in regs if kind == 1 and n == 20 for k in range(5)}
    ptrs = [i for i, w in enumerate(words) if _is_dlyptr(w)]
    sts = [next(i for i in range(p + 1, len(words)) if F(words[i])[6] == 4) for p in ptrs]
    for p, st in zip(ptrs, sts):
        if any(words[i] for i in range(p + 1, st)):
            raise ValueError(f"0x{p:03X}: wait time is not all NOP")
    out = words[:ptrs[0] - 2]
    for k, (p, st) in enumerate(zip(ptrs, sts)):
        out.extend(words[p - 2:p + 1])  # issue k
        p_at = len(out) - 1
        if k + 1 < len(ptrs):
            region = words[st + 1:ptrs[k + 1] - 2]
            j = next((i for i, w in enumerate(region) if F(w)[4] and F(w)[5] in bq), None)
            if j is None:
                raise ValueError(f"0x{p:03X}: no chain after the volume block")
            vol, chain = region[:j], region[j:]
            out.extend(chain)
            _merge_st(out, words[st], p_at)
            out.extend(vol)
        else:
            _merge_st(out, words[st], p_at)
            end = len(words)
            out.extend(words[st + 1:end])
    return _fix_jumps(words, out, ptrs[0] - 2, len(words))


def _fix_jumps(old, new, region_start, region_end_old):
    """Jumps behind the rewritten region move by the length difference."""
    delta = len(new) - len(old)
    res = list(new)
    for i, w in enumerate(res):
        a1, a2, m1, d1, m2, d2, m3, d3 = F(w)
        if a1 in dis.BRANCH and d3 >= region_end_old:
            res[i] = (w & ~0x3FF) | (d3 + delta)
    return res


def main(argv):
    if len(argv) == 3 and argv[0] == "forward":
        blocks, rest = dsp_image.parse(open(argv[1], "rb").read())
        words = fill_waits_forward(dis.program(blocks), dsp_image.regmap(blocks))
        open(argv[2], "wb").write(dsp_image.build(asm.replace_program(blocks, words), rest))
        print(f"{len(dis.program(blocks))} -> {len(words)} instructions, {asm.cycles(words)} cycles")
        return 0
    if len(argv) == 3 and argv[0] == "interleave":
        blocks, rest = dsp_image.parse(open(argv[1], "rb").read())
        words = interleave_delays(dis.program(blocks))
        open(argv[2], "wb").write(dsp_image.build(asm.replace_program(blocks, words), rest))
        print(f"{len(dis.program(blocks))} -> {len(words)} instructions, {asm.cycles(words)} cycles")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
