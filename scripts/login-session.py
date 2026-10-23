#!/usr/bin/env python3
"""User-local session selection, invoked AFTER the existing authentication gate."""
import argparse
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import termios
import time
import tty
from datetime import datetime, timezone


def settings(root):
    return json.loads((root / "deployment.json").read_text())


def set_default(root, choice):
    if choice not in ("platinum", "kde"):
        raise ValueError("Invalid default session")
    destination = root / "default-session"
    temporary = root / ("default-session." + str(os.getpid()))
    with temporary.open("x") as output:
        os.chmod(temporary, 0o600)
        output.write(choice + "\n")
        output.flush()
        os.fsync(output.fileno())
    temporary.replace(destination)


def choose(default):
    if not sys.stdin.isatty():
        return default
    print("\nPlatinum Reality login: [P] Platinum Reality  [K] KDE  [Esc] Log out", flush=True)
    print(f"Starting {default} in 5 seconds; press a key to choose.", flush=True)
    descriptor = sys.stdin.fileno()
    previous = termios.tcgetattr(descriptor)
    try:
        tty.setcbreak(descriptor)
        ready, _, _ = select.select([descriptor], [], [], 5)
        key = os.read(descriptor, 1).lower() if ready else b""
    finally:
        termios.tcsetattr(descriptor, termios.TCSADRAIN, previous)
    if key == b"k":
        return "kde"
    if key == b"p":
        return "platinum"
    if key == b"\x1b":
        return "logout"
    return default


def session_arguments(deployment):
    release = Path(deployment["release"])
    state = Path.home() / ".local/state/platinum-reality/session-runs"
    run = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + str(os.getpid())
    # Releases from before the rename call the launcher platinum-dreams.
    launcher = release / "usr/bin/platinum-reality"
    if not launcher.exists():
        launcher = release / "usr/bin/platinum-dreams"
    arguments = [sys.executable, str(release / "standalone-session.py"),
                 "--launcher", str(launcher),
                 "--evidence", str(state / run)]
    if deployment.get("primary_output"):
        arguments.extend(["--primary-output", deployment["primary_output"], "--single-output",
                          "--output-scale", str(deployment["output_scale"])])
    return arguments


# standalone-session.py --start-locked exits with this when KDE was chosen at Log In.
KDE_SESSION_EXIT = 10


def graphical_login(root, default):
    """tty1 with automatic text login: the Platinum Log In (inside the session supervisor) is the
    authentication, before any desktop exists, and it also chooses the session. Every outcome
    ends this login or starts KDE after a successful Log In: never a shell."""
    deployment = settings(root)
    arguments = session_arguments(deployment) + ["--start-locked", "--default-session", default]
    result = subprocess.run(arguments).returncode
    if result == KDE_SESSION_EXIT:
        os.execv("/usr/bin/bash", ["bash", deployment["kde_fallback"]])
    if result != 0:
        # The login service starts this again at once; a persistent fault must not spin.
        time.sleep(5)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--login", action="store_true")
    parser.add_argument("--no-prompt", action="store_true", help="Use the configured default directly")
    parser.add_argument("--graphical", action="store_true",
                        help="With --login: the Platinum Log In replaces the text login and the session prompt")
    parser.add_argument("--set-default", choices=("platinum", "kde"))
    parser.add_argument("--status", action="store_true")
    options = parser.parse_args()
    root = options.data_root.expanduser().resolve()
    if options.set_default:
        set_default(root, options.set_default)
        print("Next login default: " + options.set_default)
        return 0
    default = (root / "default-session").read_text().strip()
    if default not in ("platinum", "kde"):
        raise ValueError("Unknown default session; refusing to guess")
    if options.status:
        print("Next login default: " + default)
        return 0
    if not options.login:
        parser.error("Choose --login, --status or --set-default")
    if (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")
            or os.environ.get("XDG_VTNR") != "1"):
        print("The login selector requires tty1 after the existing authentication gate.", file=sys.stderr)
        return 2
    if options.graphical:
        return graphical_login(root, default)
    selected = default if options.no_prompt else choose(default)
    if selected == "logout":
        return 0
    deployment = settings(root)
    if selected == "kde":
        os.execv("/usr/bin/bash", ["bash", deployment["kde_fallback"]])
    arguments = session_arguments(deployment)
    # exec means ending/crashing the desktop returns to the login prompt. There
    # is no authenticated shell or automatic unlocked KDE fallback underneath.
    os.execv(sys.executable, arguments)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError) as error:
        print("Session selection failed: " + str(error), file=sys.stderr)
        raise SystemExit(1)
