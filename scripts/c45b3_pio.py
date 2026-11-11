# PlatformIO extra script: upload and helper targets through chip45boot3 (forge-tools/c45b3).
#
# Upload (VS Code: PlatformIO -> Upload, CLI: pio run -t upload):
#   own firmware jumps to the bootloader on `enter_cmd`; the original firmware does not,
#   then switch the Forge off and on while "waiting for bootloader" is shown.
# Custom targets (VS Code: PlatformIO -> Project Tasks -> Custom):
#   forge_info      bootloader/firmware version, then start the application
#   forge_backup    flash + EEPROM into backups/<date> (as far as the bootloader allows)
#   flash_release   program the official firmware 1.11.6000 of this flavour (way back)

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
base = '"$PYTHONEXE" -m c45b3 --port "$UPLOAD_PORT" --wait %s' % wait
if expect:
    # refuses to continue if the running firmware reports another model/PCB
    base += ' --expect-device "%s"' % expect
if enter:
    base += ' --enter "%s"' % enter


def need_port(target, source, env):
    if not env.subst("$UPLOAD_PORT"):
        env.AutodetectUploadPort()
    print("Forge port:", env.subst("$UPLOAD_PORT"), "| flavour:", flavour, "| expected:", expect)
    print(f"Waiting up to {wait} s for the bootloader - switch the Forge off and on "
          "if it runs the original firmware.")


env.Replace(UPLOADCMD=base + " write-flash $SOURCE --allow-write")
env.AddPreAction("upload", need_port)

env.AddCustomTarget(
    name="forge_info",
    dependencies=None,
    actions=[need_port, base + " info"],
    title="Forge: versions",
    description="Read bootloader and firmware version, then start the application",
)

stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
backup_dir = os.path.join(proj, opt("backup_dir", "backups"), stamp)
env.AddCustomTarget(
    name="forge_backup",
    dependencies=None,
    actions=[need_port, base + ' backup "%s"' % backup_dir],
    title="Forge: backup",
    description="Read flash and EEPROM through the bootloader (if the build allows it)",
)

release = os.path.join(proj, env.GetProjectOption("custom_release_hex"))
env.AddCustomTarget(
    name="flash_release",
    dependencies=None,
    actions=[need_port, base + ' write-flash "%s" --allow-write' % release],
    title="Forge: flash release 1.11.6000 (%s)" % flavour,
    description="Program the official firmware (way back)",
)
