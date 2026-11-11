# PlatformIO extra script: upload and helper targets through chip45boot3 (forge-tools/c45b3).
#
# Upload (VS Code: PlatformIO -> Upload, CLI: pio run -t upload):
#   exports the tuning into backups/<date>/tuning-before.cfg, enters the bootloader by command
#   (port firmware; with the original firmware: key next to the USB port while switching on),
#   flashes, starts, and compares the tuning afterwards (#30; option [forge] restore_tuning = yes
#   writes differences back).
# Custom targets (VS Code: PlatformIO -> Project Tasks -> Custom):
#   forge_info      bootloader/firmware version, then start the application
#   forge_backup    flash + EEPROM into backups/<date> (as far as the bootloader allows)
#   flash_release   program the official firmware 1.11.6000 of this flavour (way back)
#   forge_tuning_export   loaded preset + device settings into backups/<date>/tuning.cfg (GUI format)
#   forge_tuning_compare  compare the device with the newest tuning.cfg in backups/

import datetime
import os

Import("env")  # noqa: F821  (provided by SCons)

proj = env.subst("$PROJECT_DIR")
cfg = env.GetProjectConfig()


def opt(name, default=None):
    return cfg.get("forge", name, default)


c45b3_dir = os.path.normpath(os.path.join(proj, opt("c45b3_dir", "tools/c45b3-host")))
if not os.path.isfile(os.path.join(c45b3_dir, "c45b3", "client.py")):
    print(f"warning: c45b3 not found in {c45b3_dir} (option [forge] c45b3_dir)")

# c45b3 needs only pyserial, which PlatformIO's own Python already has
env["ENV"]["PYTHONPATH"] = c45b3_dir + os.pathsep + env["ENV"].get("PYTHONPATH", "")

wait = opt("wait_seconds", "30")
enter = opt("enter_cmd", "")
expect = env.GetProjectOption("custom_expect_device", "")
flavour = env.subst("$PIOENV")  # env name = flavour id
app_baud = opt("app_baud", "38400")  # USB-COM rate of the running firmware (reg_baud_ch1)
base = '"$PYTHONEXE" -m c45b3 --port "$UPLOAD_PORT" --wait %s --app-baud %s' % (wait, app_baud)
if expect:
    # refuses to continue if the running firmware reports another model/PCB; the identity found
    # is set in the new port firmware afterwards (builds serving several identities, #31)
    base += ' --expect-device "%s"' % expect
if enter:
    base += ' --enter "%s"' % enter


def need_port(target, source, env):
    if not env.subst("$UPLOAD_PORT"):
        env.AutodetectUploadPort()
    print("Forge port:", env.subst("$UPLOAD_PORT"), "| flavour:", flavour, "| expected:", expect)
    if enter:
        print(f"Asking the running firmware to enter the bootloader ({enter!r}); the port firmware does "
              f"this by itself. With the original firmware: within {wait} s switch the Forge off, hold "
              "the key next to the USB port, switch it on, keep holding until the orange LED blinks.")
    else:
        print(f"Waiting up to {wait} s for the bootloader: switch the Forge off, hold the key next to "
              "the USB port, switch it on, keep holding until the orange LED blinks.")


stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
backup_root = os.path.join(proj, opt("backup_dir", "backups"))
backup_dir = os.path.join(backup_root, stamp)
tuning_opts = ' --keep-tuning "%s"' % backup_dir
if opt("restore_tuning", "no").lower() in ("yes", "1", "true"):
    tuning_opts += " --restore-tuning"
env.Replace(UPLOADCMD=base + " --keep-identity write-flash $SOURCE --allow-write" + tuning_opts)
env.AddPreAction("upload", need_port)

env.AddCustomTarget(
    name="forge_info",
    dependencies=None,
    actions=[need_port, base + " info"],
    title="Forge: versions",
    description="Read bootloader and firmware version, then start the application",
)

env.AddCustomTarget(
    name="forge_backup",
    dependencies=None,
    actions=[need_port, base + ' backup "%s"' % backup_dir],
    title="Forge: backup",
    description="Read flash and EEPROM through the bootloader (if the build allows it)",
)

app_cmd = '"$PYTHONEXE" -m c45b3 --port "$UPLOAD_PORT" --app-baud %s' % app_baud
env.AddCustomTarget(
    name="forge_tuning_export",
    dependencies=None,
    actions=[need_port, 'mkdir -p "%s"' % backup_dir,
             app_cmd + ' export-tuning "%s"' % os.path.join(backup_dir, "tuning.cfg")],
    title="Forge: export tuning",
    description="Loaded preset and device settings as a GUI .cfg (read only)",
)


def newest_tuning():
    found = []
    for root, _, files in os.walk(backup_root):
        for f in files:
            if f in ("tuning.cfg", "tuning-before.cfg"):
                found.append(os.path.join(root, f))
    return max(found, key=os.path.getmtime) if found else ""


env.AddCustomTarget(
    name="forge_tuning_compare",
    dependencies=None,
    actions=[need_port, app_cmd + ' compare-tuning "%s"' % newest_tuning()],
    title="Forge: compare tuning",
    description="Compare the device with the newest exported tuning (read only)",
)

# identity -> official HEX of that variant (one build may serve several, #31)
release_assets = dict(x.strip().split(":", 1) for x in
                      env.GetProjectOption("custom_release_assets").replace("\n", " ").split() if ":" in x)
release_dir = os.path.join(proj, ".pio", "release")
release_file = os.path.join(release_dir, "selected.hex")


def fetch_release(target, source, env):
    """Ask the Forge for its identity, download the official HEX of that variant once and
    verify the pinned sha256."""
    import hashlib
    import shutil
    import subprocess
    import urllib.request
    if len(release_assets) == 1:
        ident = next(iter(release_assets))
    else:
        r = subprocess.run(env.subst('"$PYTHONEXE" -m c45b3 --port "$UPLOAD_PORT" --app-baud %s identify'
                                     % app_baud), shell=True, capture_output=True, text=True,
                           env=dict(os.environ, **env["ENV"]))
        ident = r.stdout.strip()
        if ident not in release_assets:
            print(f"error: Forge identity {ident!r} ({r.stderr.strip()}) not one of {sorted(release_assets)}; "
                  "the Forge must be on and running a firmware")
            return 1
    asset = release_assets[ident]
    sums = {}
    with open(os.path.join(proj, opt("release_sums"))) as f:
        for line in f:
            digest, name = line.split()
            sums[name] = digest
    want = sums.get(asset)
    if not want:
        print(f"error: {asset} not in {opt('release_sums')}")
        return 1
    cached = os.path.join(release_dir, asset)
    if not os.path.exists(cached):
        os.makedirs(release_dir, exist_ok=True)
        url = opt("release_url") + "/" + asset
        print("downloading", url)
        urllib.request.urlretrieve(url, cached + ".part")
        os.replace(cached + ".part", cached)
    got = hashlib.sha256(open(cached, "rb").read()).hexdigest()
    if got != want:
        os.unlink(cached)
        print(f"error: sha256 mismatch for {asset}: {got} != {want}")
        return 1
    shutil.copyfile(cached, release_file)
    print(f"{ident}: {asset}, sha256 ok")
    return 0


env.AddCustomTarget(
    name="flash_release",
    dependencies=None,
    actions=[need_port, fetch_release,
             base + ' write-flash "%s" --allow-write' % release_file],
    title="Forge: flash release 1.11.6000 (%s)" % flavour,
    description="Program the official firmware of the connected variant (GitHub release 0.0.1116000)",
)
