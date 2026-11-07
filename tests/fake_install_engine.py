#!/usr/bin/env python3
"""A stand-in for the install engine (HANDOFF.md contract) for installer tests. Touches no disk.

FAKE_ENGINE_LOG names a JSON-lines file recording each call (arguments, the config, and only the
length of the password it was given). `choices` answers unless FAKE_ENGINE_NO_CHOICES=1 (an engine
from before that command). FAKE_ENGINE_FAIL=STEP makes install fail at that step (1 means
"format"). It refuses what Rocky's engine refuses (distro/installer-engine/engine.py validate_config
and read_secrets), so the window's own checks must match the engine's.
"""
import json
import os
import re
import sys
import time


def refusal(config, secrets):
    """The engine's account, hostname and secret rules; None when it would accept them."""
    host, user = config.get("hostname"), config.get("user") or {}
    if not isinstance(host, str) or not re.fullmatch(r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?", host):
        return "Hostname must be one lowercase DNS label"
    name, full = user.get("name"), user.get("full_name")
    if (not isinstance(name, str) or not re.fullmatch(r"[a-z_][a-z0-9_-]{0,30}", name)
            or name in {"root", "liveuser", "sddm", "nobody"}):
        return "Choose a new regular Linux account name"
    if not isinstance(full, str) or not 1 <= len(full) <= 128 or any(ord(c) < 32 or c in ":\x7f" for c in full):
        return "Full name contains unsupported account-field characters"
    for value in secrets.values():
        if not isinstance(value, str) or not 8 <= len(value) <= 1024 or any(c in value for c in "\x00\n\r"):
            return "Secrets must be 8–1024 characters with no NUL or newline"
    return None

log = os.environ.get("FAKE_ENGINE_LOG")


def note(record):
    if log:
        with open(log, "a") as stream:
            stream.write(json.dumps(record) + "\n")


arguments = sys.argv[1:]
command = arguments[0] if arguments else ""
if command == "disks" and arguments[1:] == ["--json"]:
    note({"argv": arguments})
    disks = [
        {"path": "/dev/sda", "model": "Platinum USB", "size_bytes": 16_000_000_000, "removable": True,
         "transport": "usb", "is_boot_medium": True, "partitions": []},
        {"path": "/dev/vda", "model": "Hard Disk", "size_bytes": 64_000_000_000, "removable": False,
         "transport": "virtio", "is_boot_medium": False,
         "partitions": [{"path": "/dev/vda1", "fstype": "ext4", "label": "old", "size_bytes": 64_000_000_000}]}]
    if os.environ.get("FAKE_ENGINE_SMALL_DISKS"):
        # As QEMU lists them (the update-37 image's first actual R3): an empty CD drive, then a stick
        # too small to install on, both before the hard disk.
        disks[1:1] = [{"path": "/dev/sr0", "model": "QEMU DVD-ROM", "size_bytes": 0, "removable": True,
                       "transport": "sata", "is_boot_medium": False, "partitions": []},
                      {"path": "/dev/sdb", "model": "Small Stick", "size_bytes": 8_000_000_000, "removable": True,
                       "transport": "usb", "is_boot_medium": False, "partitions": []}]
    print(json.dumps(disks))
    sys.exit(0)
config = {}
if "--config" in arguments:
    with open(arguments[arguments.index("--config") + 1]) as stream:
        config = json.load(stream)
if command == "choices" and arguments[1:] == ["--json"] and os.environ.get("FAKE_ENGINE_NO_CHOICES") != "1":
    time.sleep(0.3)  # Long enough for the window to ask for something else meanwhile.
    print(json.dumps({"locales": ["de_DE.UTF-8", "en_GB.UTF-8", "en_US.UTF-8", "bad locale!"],
                      "timezones": ["America/Denver", "Europe/Berlin", "UTC"], "keymaps": ["de", "us"]}))
    sys.exit(0)
if command == "plan":
    note({"argv": arguments, "config": config})
    # As the engine (Rocky's 72af54b): the chosen disk's partitions, and a warning when there are any.
    label = os.environ.get("FAKE_ENGINE_LABEL", "old")
    partitions = [{"path": "/dev/vda1", "fstype": "ext4", "label": label, "size_bytes": 64_000_000_000}]
    print(json.dumps({"ok": config.get("disk") == "/dev/vda", "erases": [config.get("disk")],
                      "existing_partitions": partitions,
                      "steps": [{"id": "format", "title": "Erase the disk"}],
                      "warnings": [f"The destination already contains {len(partitions)} partition(s); "
                                   "every listed partition will be removed."]}))
    sys.exit(0)
if command == "install":
    secrets = json.loads(sys.stdin.readline() or "{}")
    confirm = arguments[arguments.index("--confirm-erase") + 1]
    note({"argv": arguments, "config": config, "password_length": len(secrets.get("password", "")),
          "passphrase_length": len(secrets.get("passphrase", ""))})
    if confirm != config.get("disk"):
        print(json.dumps({"result": "error", "message": "The disk was not confirmed.", "step": "check"}), flush=True)
        sys.exit(1)
    fail = {"1": "format"}.get(os.environ.get("FAKE_ENGINE_FAIL", ""), os.environ.get("FAKE_ENGINE_FAIL"))
    problem = refusal(config, secrets)
    # The engine's steps; it starts erasing at "partition".
    for step, fraction, message in (("validate", 0.0, "Checking the installation…"), ("partition", 0.1, "Erasing the disk…"),
                                    ("format", 0.2, "Creating the file systems…"), ("packages", 0.4, "Installing software…"),
                                    ("configure", 0.7, "Setting up your account…"), ("bootloader", 0.85, "Making the disk start up…"),
                                    ("finish", 0.95, "Finishing…")):
        print(json.dumps({"step": step, "fraction": fraction, "message": message}), flush=True)
        time.sleep(0.02)
        if step == "validate" and problem:
            print(json.dumps({"result": "error", "message": problem, "step": step}), flush=True)
            sys.exit(1)
        if step == fail:
            print(json.dumps({"result": "error", "message": "The disk could not be erased." if step == "format"
                              else f"The {step} step failed.", "step": step}), flush=True)
            sys.exit(1)
    print(json.dumps({"result": "ok"}), flush=True)
    sys.exit(0)
sys.exit(2)
