#!/usr/bin/env python3
"""Disposable activation/prompt fixture. Implements no credential or KWallet API."""
import argparse
import json
import os
from pathlib import Path
import signal

import gi
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--name", required=True)
parser.add_argument("--log", type=Path, required=True)
parser.add_argument("--window", action="store_true")
options = parser.parse_args()


def record(event):
    with options.log.open("a") as output:
        output.write(json.dumps(event) + "\n")


bus = Gio.DBusConnection.new_for_address_sync(
    os.environ["DBUS_SESSION_BUS_ADDRESS"],
    Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
    None, None)
bus.set_exit_on_close(False)
loop = GLib.MainLoop()
# Deliberately stay alive after a bus disconnect. The session-owned supervisor
# must terminate this process; otherwise lifetime assertions would be vacuous.
xml = """<node><interface name="org.platinum_reality.WalletFixture">
<method name="Ready"><arg type="s" direction="out"/></method>
</interface></node>"""
info = Gio.DBusNodeInfo.new_for_xml(xml)


def called(_connection, _sender, _path, _interface, _method, _parameters, invocation):
    invocation.return_value(GLib.Variant("(s)", (options.name,)))


bus.register_object("/org/platinum_reality/WalletFixture", info.interfaces[0], called, None, None)
result = bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
                       GLib.Variant("(su)", (options.name, 0)), GLib.VariantType.new("(u)"),
                       Gio.DBusCallFlags.NONE, 5000, None)
assert result.unpack() == (1,)
record({"event": "started", "name": options.name, "pid": os.getpid(), "supervisor_pid": os.getppid(),
        "wayland_socket_present": "WAYLAND_SOCKET" in os.environ,
        "environment": {key: os.environ.get(key, "") for key in (
            "HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR",
            "DBUS_SESSION_BUS_ADDRESS", "WAYLAND_DISPLAY", "DISPLAY", "XAUTHORITY", "QT_QPA_PLATFORM",
            "QT_PLUGIN_PATH", "QT_STYLE_OVERRIDE", "XDG_CURRENT_DESKTOP", "WAYLAND_SOCKET")}})
if options.window:
    gi.require_version("Gtk", "3.0")
    from gi.repository import Gtk
    assert Gtk.init_check()[0]
    window = Gtk.Window(title="Wallet unlock fixture")
    window.set_default_size(360, 120)
    window.add(Gtk.Label(label="Disposable prompt fixture. No passwords or wallet data."))
    window.connect("delete-event", lambda *_: True)
    window.show_all()
for sig in (signal.SIGTERM, signal.SIGINT):
    GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, sig, lambda: (loop.quit(), False)[1])
loop.run()
record({"event": "stopped", "name": options.name, "pid": os.getpid()})
