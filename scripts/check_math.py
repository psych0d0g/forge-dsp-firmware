# Post-build check: with -mdouble=64 no math function may resolve to its float version
# (avr-libc defines sin, cos, ... as weak aliases of sinf, cosf, ... in the 32-bit libm).
# This happened while the link step lacked -mdouble=64 (wrong multilib, issue #34).
import subprocess
Import("env")


def check(target, source, env):
    elf = str(target[0])
    nm = env.subst("$CC").replace("gcc", "nm")
    syms = {}
    for line in subprocess.check_output([nm, elf], text=True).splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms.setdefault(parts[2], parts[0])
    bad = [f for f in ("sin", "cos", "tan", "sqrt", "pow", "exp", "log", "log10", "atan", "atan2",
                       "floor", "ceil", "round", "lround", "trunc", "fabs", "fmod", "ldexp")
           if f in syms and f + "f" in syms and syms[f] == syms[f + "f"]]
    if bad:
        print("ERROR: 64-bit double math resolved to float versions: " + ", ".join(bad))
        env.Exit(1)
    print("math check: no float aliases")


if "-mdouble=64" in env.ParseFlags(env.GetProjectOption("build_flags"))["CCFLAGS"]:
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check)
