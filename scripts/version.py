# PlatformIO pre script: firmware version from git.
#   tag v1.2.3            -> "1.2.3",        FW_VERSION:01.02 (chip45boot3 command 0x12)
#   3 commits after tag   -> "1.2.3-3-gabcdef0"
#   no tag                -> "0.0.0-g<sha>"
#   uncommitted changes   -> suffix "-dirty"
# Build date = commit date (reproducible), as main_build_date in system.key.info.
import re
import subprocess

Import("env")  # noqa: F821


def git(*args):
    try:
        return subprocess.check_output(["git", *args], cwd=env.subst("$PROJECT_DIR"),
                                       stderr=subprocess.DEVNULL, text=True).strip()
    except Exception:
        return ""


desc = git("describe", "--tags", "--match", "v[0-9]*", "--dirty")
if desc:
    version = desc[1:]
else:
    sha = git("rev-parse", "--short=7", "HEAD") or "unknown"
    version = "0.0.0-g" + sha + ("-dirty" if git("status", "--porcelain", "--untracked-files=no") else "")
m = re.match(r"(\d+)\.(\d+)", version)
major, minor = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
date = git("log", "-1", "--format=%cd", "--date=format:%Y%m%d") or "00000000"

env.Append(CPPDEFINES=[
    ("FW_VERSION_STR", '\\"%s\\"' % version),
    ("FW_VERSION_C45B3", '\\"FW_VERSION:%02d.%02d\\"' % (major % 100, minor % 100)),
    ("FW_BUILD_DATE", '\\"%s\\"' % date),
])
env["FORGE_VERSION"] = version
print("forge firmware version", version, "build date", date)
