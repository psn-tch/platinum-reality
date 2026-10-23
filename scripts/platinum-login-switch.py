#!/usr/bin/env python3
"""psn_tch runs this themselves: switch tty1 from the text fingerprint gate to the Platinum Log In, or back.

tty1 already logs in automatically (getty@tty1 autologin.conf), so today the fingerprint gate is
the only authentication there, and three failed scans leave an unauthenticated shell. With the
Platinum Log In, login-session.py --graphical authenticates before any desktop and never gives a
shell. Only the tty1 block of ~/.bash_profile changes. It moves above the console-rotation sudo
line, so no sudo prompt appears at boot; other consoles keep the rotation. The exact previous file
is kept and "off" restores it, refusing if the profile changed in between. Nothing else is touched.
"""
import argparse
import hashlib
import os
from pathlib import Path
import sys
import tempfile

HOME = Path.home()
PROFILE = HOME / ".bash_profile"
ROOT = HOME / ".local/share/platinum-dreams-native"
BACKUP = ROOT / "bash_profile.before-platinum-login"
SELECTOR = ROOT / "login-session.py"

ROTATE = "[ -t 0 ] && echo 3 | sudo tee /sys/class/graphics/fbcon/rotate > /dev/null\n"
TEXT_GATE = (
    'if [ -z "${DISPLAY}" ] && [ "${XDG_VTNR}" -eq 1 ] && [ "$(tty)" = /dev/tty1 ]; then\n'
    "\tmkdir -p ~/.cache\n"
    f"\t~/.fingerprint-gate.sh && exec /usr/bin/python3 {SELECTOR} --login\n"
    "fi \n")
LOG_IN = (
    "# tty1 logs in automatically; the Platinum Log In is the authentication, before any desktop,\n"
    "# and it never gives a shell. Undo: platinum-login-switch.py off\n"
    'if [ -z "${DISPLAY}" ] && [ "${XDG_VTNR}" -eq 1 ] && [ "$(tty)" = /dev/tty1 ]; then\n'
    "\tmkdir -p ~/.cache\n"
    f"\texec /usr/bin/python3 {SELECTOR} --login --graphical\n"
    "\texit 1\n"
    "fi\n")


def write_atomically(path, data, mode):
    descriptor, name = tempfile.mkstemp(dir=path.parent, prefix="." + path.name + ".")
    try:
        with os.fdopen(descriptor, "w") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(name, mode)
        os.replace(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


def switch_on():
    if "--graphical" not in SELECTOR.read_text():
        sys.exit("The selected release's login-session.py has no Platinum Log In yet; select that release first.")
    current = PROFILE.read_text()
    if current.count(ROTATE) != 1 or current.count(TEXT_GATE) != 1 or not current.endswith(TEXT_GATE):
        sys.exit("~/.bash_profile is not the version this script knows; nothing changed.")
    if BACKUP.exists():
        sys.exit(f"{BACKUP} already exists; nothing changed.")
    mode = PROFILE.stat().st_mode & 0o777
    with BACKUP.open("x") as backup:
        backup.write(current)
    head = current[:current.index(ROTATE)]
    between = current[current.index(ROTATE) + len(ROTATE):current.index(TEXT_GATE)]
    write_atomically(PROFILE, head + LOG_IN + "\n" + ROTATE + between.rstrip("\n") + "\n", mode)
    print("tty1 now uses the Platinum Log In from the next login. Backup:", BACKUP)
    print("sha256", hashlib.sha256(PROFILE.read_bytes()).hexdigest())


def switch_off():
    if not BACKUP.exists():
        sys.exit("No backup from platinum-login-switch.py; nothing changed.")
    if LOG_IN not in PROFILE.read_text():
        sys.exit("~/.bash_profile changed since the switch; restore the backup by hand: " + str(BACKUP))
    write_atomically(PROFILE, BACKUP.read_text(), PROFILE.stat().st_mode & 0o777)
    BACKUP.unlink()
    print("tty1 uses the text fingerprint gate again from the next login.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("state", choices=("on", "off"))
    switch_on() if parser.parse_args().state == "on" else switch_off()
