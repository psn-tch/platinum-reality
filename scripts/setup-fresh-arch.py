#!/usr/bin/env python3
"""VM-only fresh-Arch post-install prototype. Planning is the default; never partitions a disk."""
import argparse
import configparser
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import shutil
import subprocess
import sys
import stat
import io
import tempfile

EXTRAS = ["sddm", "xorg-server", "pipewire-audio", "pipewire-pulse", "wireplumber", "alsa-utils",
          "ttf-dejavu", "noto-fonts", "konsole", "firefox", "plasma-integration", "iwd", "bluez",
          "bluez-utils", "brightnessctl", "xdg-user-dirs", "noto-fonts-emoji", "rtkit"]
IWD = "[General]\nEnableNetworkConfiguration=true\n\n[Network]\nNameResolvingService=systemd\n"
SDDM_SESSION = "/usr/share/wayland-sessions/platinum-reality.desktop"
WIRED = "[Match]\nName=en* eth*\n\n[Network]\nDHCP=yes\n"


def package_info(package, expected):
    with package.open("rb") as source:
        actual = hashlib.file_digest(source, "sha256").hexdigest()
    if not re.fullmatch(r"[a-f0-9]{64}", expected) or actual != expected:
        raise ValueError("Package SHA-256 does not match the reviewed input")
    result = subprocess.run(["bsdtar", "-xOf", str(package), ".PKGINFO"],
                            check=True, text=True, capture_output=True)
    info = {}
    for line in result.stdout.splitlines():
        if " = " in line and not line.startswith("#"):
            key, value = line.split(" = ", 1)
            info.setdefault(key, []).append(value)
    if info.get("pkgname") != ["platinum-reality"] or info.get("arch") != ["x86_64"]:
        raise ValueError("Expected an x86_64 platinum-reality package")
    dependencies = []
    for value in info.get("depend", []):
        name = re.split(r"[<>=]", value, maxsplit=1)[0]
        if not re.fullmatch(r"[a-z0-9][a-z0-9@+_.-]*", name):
            raise ValueError("Unexpected dependency syntax")
        dependencies.append(name)
    return info, sorted(set(dependencies))


def build_plan(package, sha256, network):
    info, dependencies = package_info(package, sha256)
    files = {}
    services = ["sddm.service", "bluetooth.service"]
    if network == "iwd-networkd":
        files = {"/etc/iwd/main.conf": IWD,
                 "/etc/systemd/network/20-platinum-reality-wired.network": WIRED}
        services += ["iwd.service", "systemd-networkd.service", "systemd-resolved.service"]
    return {"prototype": "VM ONLY; existing fresh Arch with a user and working internet",
        "package": str(package), "sha256": sha256, "version": info.get("pkgver"),
        "network": network, "packages": sorted(set(dependencies + EXTRAS)), "files": files,
        "services_enabled_for_next_boot": services,
        "global_user_units": ["pipewire.socket", "pipewire-pulse.socket", "wireplumber.service"],
        "default_target": "graphical.target", "autologin": False,
        "sddm_preselected_session": SDDM_SESSION,
        "create_standard_user_directories": True,
        "reboot_automatically": False}


@contextmanager
def staged_package(package, sha256):
    # Apply runs as root: the directory is root-owned 0700. The user-supplied
    # package path is never passed to pacman after verification.
    # Ignore an inherited TMPDIR: an unprivileged parent-directory owner could
    # rename a root-created child there and substitute the verified pathname.
    with tempfile.TemporaryDirectory(prefix="platinum-reviewed-package-", dir="/var/tmp") as directory:
        staged = Path(directory) / "platinum-reality.pkg.tar.zst"
        with package.open("rb") as source, staged.open("xb") as destination:
            shutil.copyfileobj(source, destination)
        os.chmod(staged, 0o600)
        package_info(staged, sha256)
        yield staged


def require_local_password(shadow, user):
    entries = [line.split(":") for line in shadow.splitlines() if line.split(":", 1)[0] == user]
    if (len(entries) != 1 or len(entries[0]) < 2 or not entries[0][1] or
            entries[0][1].startswith(("!", "*"))):
        raise ValueError("Set a local login password for the target user before installing")


def require_sddm_stopped():
    result = subprocess.run(["systemctl", "show", "sddm.service", "--property=ActiveState", "--value"],
                            capture_output=True, text=True)
    if result.returncode or result.stdout.strip() not in ("inactive", "failed"):
        raise ValueError("SDDM must be stopped before applying setup; its live session will not be changed")


def updated_sddm_state(contents, session):
    # SDDM documents ~sddm/state.conf [Last] Session as the greeter selection.
    # No [Autologin] configuration is created. Keep the other state values.
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    try:
        parser.read_string(contents)
    except configparser.Error as error:
        raise ValueError("Cannot safely parse SDDM state.conf") from error
    if not parser.has_section("Last"):
        parser.add_section("Last")
    parser.set("Last", "Session", session)
    output = io.StringIO()
    parser.write(output, space_around_delimiters=False)
    return output.getvalue()


def preselect_sddm_session(account, session):
    require_sddm_stopped()  # Check again after package installation.
    if not Path(session).is_file():
        raise ValueError("Installed Platinum Reality session entry is missing")
    # Anchor all operations to a checked directory; never follow a state-file
    # symlink controlled by the unprivileged greeter account.
    directory = os.open(account.pw_dir, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    temporary = ".platinum-state-" + os.urandom(12).hex()
    created = False
    try:
        info = os.fstat(directory)
        if info.st_uid not in (0, account.pw_uid) or info.st_mode & 0o022:
            raise ValueError("Unsafe SDDM home owner or permissions")
        contents = ""
        try:
            existing = os.open("state.conf", os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
        except FileNotFoundError:
            pass
        else:
            with os.fdopen(existing, "r", encoding="utf-8") as source:
                info = os.fstat(source.fileno())
                if not stat.S_ISREG(info.st_mode) or info.st_uid not in (0, account.pw_uid):
                    raise ValueError("Unsafe SDDM state file")
                contents = source.read(65537)
                if len(contents) > 65536:
                    raise ValueError("SDDM state file is unexpectedly large")
        updated = updated_sddm_state(contents, session)
        descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                             0o600, dir_fd=directory)
        created = True
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            output.write(updated)
            output.flush()
            os.fchmod(output.fileno(), 0o600)
            os.fchown(output.fileno(), account.pw_uid, account.pw_gid)
            os.fsync(output.fileno())
        os.replace(temporary, "state.conf", src_dir_fd=directory, dst_dir_fd=directory)
        created = False
        os.fsync(directory)
    finally:
        if created:
            os.unlink(temporary, dir_fd=directory)
        os.close(directory)


def target_checks(plan, user):
    if os.geteuid() != 0:
        raise ValueError("Apply must run as root INSIDE the disposable guest")
    if not Path("/etc/arch-release").is_file():
        raise ValueError("Requires Arch Linux")
    # Deliberately not a public machine installer yet. This guard must remain
    # until a reviewed physical-machine installation phase is authorized.
    virtual = subprocess.run(["systemd-detect-virt", "--vm"], capture_output=True, text=True)
    if virtual.returncode or virtual.stdout.strip() not in ("qemu", "kvm"):
        raise ValueError("Prototype refuses physical hosts and containers")
    container = subprocess.run(["systemd-detect-virt", "--container"], capture_output=True, text=True)
    if container.returncode == 0:
        raise ValueError("Prototype refuses containers, including containers inside VMs")
    account = pwd.getpwnam(user)
    if account.pw_uid < 1000 or not Path(account.pw_dir).is_dir():
        raise ValueError("A regular pre-existing user and home are required")
    require_local_password(Path("/etc/shadow").read_text(), user)
    require_sddm_stopped()
    display = Path("/etc/systemd/system/display-manager.service")
    if display.is_symlink() and display.resolve().name != "sddm.service":
        raise ValueError("An existing display manager must not be replaced by this prototype")
    if plan["network"] == "iwd-networkd":
        for service in ["NetworkManager.service", "wpa_supplicant.service", "dhcpcd.service"]:
            if (subprocess.run(["systemctl", "is-active", "--quiet", service]).returncode == 0 or
                subprocess.run(["systemctl", "is-enabled", "--quiet", service],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0):
                raise ValueError("Existing network owner: " + service + "; use preserve-wired or review manually")
        resolver = Path("/etc/resolv.conf")
        if resolver.is_symlink():
            if resolver.resolve(strict=False) != Path("/run/systemd/resolve/stub-resolv.conf"):
                raise ValueError("Existing resolver link is not the systemd-resolved stub")
        elif resolver.exists():
            raise ValueError("Existing /etc/resolv.conf will not be overwritten")
    for filename, contents in plan["files"].items():
        path = Path(filename)
        if path.is_symlink() or (path.exists() and path.read_text() != contents):
            raise ValueError("Existing configuration differs: " + filename)
    return account


def apply(plan, user):
    account = target_checks(plan, user)  # Before any package or configuration mutation.
    commands = []
    def command(args):
        print("+ " + " ".join(args), flush=True)
        subprocess.run(args, check=True)
        commands.append(args)
    # A full upgrade, never -Sy followed by selective package installation.
    with staged_package(Path(plan["package"]), plan["sha256"]) as package:
        # Derive the root operation from the verified private copy as well.
        # The original path could change between its initial hash and metadata
        # read; its untrusted metadata must not select extra repository packages.
        verified_plan = build_plan(package, plan["sha256"], plan["network"])
        verified_plan["package"] = plan["package"]
        if verified_plan != plan:
            raise ValueError("Package plan differs from the verified private copy; review again")
        command(["pacman", "-Syu", "--needed", "--noconfirm", "--", *plan["packages"]])
        # Never --needed here: a rebuilt package with the same version would be skipped, leaving
        # older program files in place of the reviewed bytes (Rocky, 2026-10-23).
        command(["pacman", "-U", "--noconfirm", "--", str(package)])
    for filename, contents in plan["files"].items():
        path = Path(filename)
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists():
            with path.open("x") as stream:
                stream.write(contents)
            os.chmod(path, 0o644)
    if plan["network"] == "iwd-networkd" and not Path("/etc/resolv.conf").is_symlink():
        Path("/etc/resolv.conf").symlink_to("/run/systemd/resolve/stub-resolv.conf")
    # Use the package's own owner/mode definitions even before SDDM first runs.
    command(["systemd-tmpfiles", "--create", "/usr/lib/tmpfiles.d/sddm.conf"])
    preselect_sddm_session(pwd.getpwnam("sddm"), plan["sddm_preselected_session"])
    command(["systemctl", "enable", *plan["services_enabled_for_next_boot"]])
    command(["systemctl", "--global", "enable", *plan["global_user_units"]])
    command(["runuser", "-u", user, "--", "env", "HOME=" + account.pw_dir,
             "XDG_CONFIG_HOME=" + account.pw_dir + "/.config",
             "xdg-user-dirs-update"])
    command(["systemctl", "set-default", "graphical.target"])
    command(["pacman", "-Qk", "platinum-reality"])
    receipt = {**plan, "target_user": user, "completed_utc": datetime.now(timezone.utc).isoformat(),
               "commands_completed": commands,
               "recovery": "Select another session or graphical/text console; OS package upgrades are not rolled back"}
    destination = Path("/var/lib/platinum-reality-installer")
    destination.mkdir(mode=0o700, parents=True, exist_ok=True)
    (destination / "receipt.json").write_text(json.dumps(receipt, indent=2)+"\n")
    print("Installed. Reboot when ready; SDDM will preselect Platinum Reality. No autologin configured.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--network", choices=["iwd-networkd", "preserve-wired"], default="iwd-networkd")
    parser.add_argument("--user", help="Existing local account to configure; required for apply")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--confirm-fresh-arch", action="store_true")
    args = parser.parse_args()
    package = args.package.resolve(strict=True)
    plan = build_plan(package, args.sha256, args.network)
    if args.apply:
        if not args.confirm_fresh_arch:
            parser.error("Apply requires --confirm-fresh-arch")
        if not args.user:
            parser.error("Apply requires an explicit --user")
        apply(plan, args.user)
    else:
        print(json.dumps(plan, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
