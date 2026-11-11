# PlatformIO post script: copy firmware.hex to dist/forge-dsp-<flavour>-<version>.hex (+ .sha256)
import hashlib
import os
import shutil

Import("env")  # noqa: F821


def dist(target, source, env):
    hexfile = str(target[0])
    out = os.path.join(env.subst("$PROJECT_DIR"), "dist")
    os.makedirs(out, exist_ok=True)
    name = "forge-dsp-%s-%s.hex" % (env.subst("$PIOENV"), env.get("FORGE_VERSION", "unknown"))
    dst = os.path.join(out, name)
    shutil.copyfile(hexfile, dst)
    digest = hashlib.sha256(open(dst, "rb").read()).hexdigest()
    with open(dst + ".sha256", "w") as f:
        f.write(f"{digest}  {name}\n")
    print("dist:", dst)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", dist)
