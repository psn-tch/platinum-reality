#!/usr/bin/env python3
"""Support private nested sessions; never change host D-Bus policy."""
import ast
import configparser
import os
from pathlib import Path
import pwd
import stat
import shlex
import subprocess
import sys
from urllib.parse import quote
import xml.etree.ElementTree as ET


WALLET_SERVICES = ("org.kde.kwalletd6", "org.kde.secretservicecompat", "org.kde.secretprompter")
WALLET_SERVICE_DIRECTORY = Path("/usr/share/dbus-1/services")


def adopt_previous_name(data):
    """Platinum Reality was called Platinum Dreams. Its settings folder <data>/platinum-dreams (the
    profile, imported assets and the startup screen drawing) moves to <data>/platinum-reality once,
    leaving a link with the old name for older releases. Never replaces anything; links are left alone."""
    old, new = Path(data) / "platinum-dreams", Path(data) / "platinum-reality"
    if not old.is_dir() or old.is_symlink() or new.exists() or new.is_symlink():
        return False
    try:
        os.rename(old, new)
    except OSError:
        return False
    try:
        old.symlink_to("platinum-reality")
    except OSError:
        pass
    return True


def wallet_login_requested(environment, private_settings=False):
    """A one-use allowance from the physical supervisor, never inherited by nested clients."""
    return (not private_settings and environment.get("PLATINUM_LOGIN_SESSION") == "1"
            and environment.get("PLATINUM_WALLET_LOGIN") == "1"
            and environment.get("FRANKENSTEIN_ISOLATED") != "1"
            and environment.get("FRANKENSTEIN_NATIVE_TEST") != "1")


def host_wallet_status(environment, run=subprocess.run, user_bus=None):
    """Inspect existing names only; never activate or open a wallet on the host bus.

    An unreachable bus is unknown, not evidence that no other daemon has the wallet open.
    """
    addresses = []
    if environment.get("DBUS_SESSION_BUS_ADDRESS"):
        addresses.append(environment["DBUS_SESSION_BUS_ADDRESS"])
    user_bus = Path(user_bus) if user_bus is not None else Path(f"/run/user/{os.getuid()}/bus")
    if user_bus.exists():
        addresses.append("unix:path=" + quote(str(user_bus), safe="/"))
    for address in dict.fromkeys(addresses):
        try:
            result = run(["gdbus", "call", "--address", address, "--dest", "org.freedesktop.DBus",
                          "--object-path", "/org/freedesktop/DBus", "--method",
                          "org.freedesktop.DBus.ListNames"], capture_output=True, text=True,
                         stdin=subprocess.DEVNULL, timeout=3, check=False)
            if result.returncode:
                return "unknown"
            names, = ast.literal_eval(result.stdout.strip())
            if not isinstance(names, list) or not all(isinstance(name, str) for name in names):
                return "unknown"
        except (OSError, ValueError, SyntaxError, TypeError, subprocess.TimeoutExpired):
            return "unknown"
        if set(names).intersection((*WALLET_SERVICES, "org.freedesktop.secrets")):
            return "busy"
    return "idle"


def prepare_wallet_services(runtime, supervisor, service_directory=WALLET_SERVICE_DIRECTORY):
    """Allow only the installed wallet services, with session-owned process lifetime.

    Reconstruct Name/Exec rather than importing systemd activation or host service dirs.
    The alternate source directory exists for disposable fake-service qualification only.
    """
    supervisor = Path(supervisor).absolute()
    if not supervisor.is_file() or not os.access(supervisor, os.X_OK):
        raise ValueError("The session wallet supervisor is missing.")
    contents = {}
    for name in WALLET_SERVICES:
        source = Path(service_directory) / (name + ".service")
        if not source.is_file() or source.stat().st_size > 16384:
            raise ValueError("A required KDE wallet service is missing.")
        parser = configparser.ConfigParser(interpolation=None, strict=True)
        try:
            parser.read_string(source.read_text())
            section = parser["D-BUS Service"]
            command = shlex.split(section["Exec"])
            if section["Name"] != name or not command or not Path(command[0]).is_absolute():
                raise ValueError("Invalid KDE wallet service.")
            if not Path(command[0]).is_file() or not os.access(command[0], os.X_OK):
                raise ValueError("A required KDE wallet executable is missing.")
        except (configparser.Error, KeyError, UnicodeError) as error:
            raise ValueError("Invalid KDE wallet service.") from error
        # dbus-daemon uses shell-style argument parsing, without running a shell.
        invocation = shlex.join([str(supervisor), "--", *command])
        contents[name + ".service"] = "[D-BUS Service]\nName=" + name + "\nExec=" + invocation + "\n"
    directory = Path(runtime) / "wallet-services"
    directory.mkdir(mode=0o700, exist_ok=False)
    for filename, content in contents.items():
        (directory / filename).write_text(content)
    return directory


STOCK_SESSION_LIMITS = {
    "max_incoming_bytes": "1000000000",
    "max_incoming_unix_fds": "250000000",
    "max_outgoing_bytes": "1000000000",
    "max_outgoing_unix_fds": "250000000",
    "service_start_timeout": "120000",
    "auth_timeout": "240000",
    "pending_fd_timeout": "150000",
    "max_completed_connections": "100000",
    "max_incomplete_connections": "10000",
    "max_connections_per_user": "100000",
    "max_pending_service_starts": "10000",
    "max_names_per_connection": "50000",
    "max_match_rules_per_connection": "50000",
    "max_replies_per_connection": "50000",
}


def write_bus_config(runtime, *, wallet_supervisor=None, service_directory=WALLET_SERVICE_DIRECTORY):
    runtime = Path(runtime)
    config = ET.Element("busconfig")
    ET.SubElement(config, "type").text = "session"
    ET.SubElement(config, "keep_umask")
    ET.SubElement(config, "listen").text = "unix:path=" + quote(str(runtime / "bus"), safe="/")
    ET.SubElement(config, "auth").text = "EXTERNAL"
    policy = ET.SubElement(config, "policy", context="default")
    ET.SubElement(policy, "deny", user="*")
    ET.SubElement(policy, "allow", user=pwd.getpwuid(os.getuid()).pw_name)
    ET.SubElement(policy, "allow", send_destination="*")
    ET.SubElement(policy, "allow", receive_sender="*")
    ET.SubElement(policy, "allow", own="*")
    # Nested sessions and all ordinary native tests remain activation-free. A physical
    # login may explicitly allow the three wallet services; never include the host dirs.
    if wallet_supervisor is not None:
        directory = prepare_wallet_services(runtime, wallet_supervisor, service_directory)
        ET.SubElement(config, "servicedir").text = str(directory)
    ET.SubElement(config, "limit", name="max_message_size").text = "16777216"
    # The stock session bus's per-connection limits (/usr/share/dbus-1/session.conf). Without
    # them dbus-daemon's built-in defaults apply, e.g. 128 pending replies per connection. A
    # call to an application that never answers holds its slot until that application leaves,
    # even after the caller gives up, so the desktop ran out on 2026-10-21 ("maximum number of
    # pending replies ... has been reached") and its later calls failed. Like the stock bus,
    # there is no reply timeout: some calls rightly wait for the user (a wallet password).
    for name, value in STOCK_SESSION_LIMITS.items():
        ET.SubElement(config, "limit", name=name).text = value
    path = runtime / "bus.conf"
    path.write_text(ET.tostring(config, encoding="unicode") + "\n")
    return path


def cleanup_runtime(runtime):
    runtime = Path(runtime).absolute()
    if (runtime.parent != Path("/tmp") or not runtime.name.startswith("frankenstein-runtime.")
            or runtime.is_symlink() or runtime.stat().st_uid != os.getuid()):
        raise ValueError("Refusing cleanup outside an owned Frankenstein runtime")
    for entry in runtime.iterdir():
        mode = entry.lstat().st_mode
        # Unlink only known files or sockets created in this private runtime.
        # Empty, owned helper directories can be removed without descending.
        # rmdir rejects nonempty directories, symlinks and mounted portal roots.
        if stat.S_ISSOCK(mode) or (stat.S_ISREG(mode) and entry.name in (
                "bus.conf", "frankenstein-qualification.lock")):
            entry.unlink()
        elif stat.S_ISDIR(mode) and entry.lstat().st_uid == os.getuid():
            if entry.name == "wallet-services":
                for filename in (name + ".service" for name in WALLET_SERVICES):
                    service = entry / filename
                    if service.exists() and stat.S_ISREG(service.lstat().st_mode):
                        service.unlink()
            try:
                entry.rmdir()
            except OSError:
                pass  # Retain unexpected contents; never recursively delete.
    try:
        runtime.rmdir()
    except OSError:
        return False
    return True


if __name__ == "__main__":
    operation, runtime = sys.argv[1:]
    if operation == "configure":
        print(write_bus_config(runtime))
    elif operation == "cleanup":
        if cleanup_runtime(runtime):
            print("Private runtime removed safely.")
        else:
            print("Runtime retained because it contains unexpected entries; no recursive deletion: " + runtime)
            sys.exit(1)
    else:
        raise SystemExit("Unknown session-support operation")
