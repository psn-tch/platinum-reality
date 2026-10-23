#!/usr/bin/env python3
"""A fake BlueZ for session qualification (docs/bluetooth.md). Never the host's system bus.

Owns org.bluez on the private bus given by --address with one adapter, one paired device and
an agent manager. Every call is recorded with the caller's PID in --log (JSON lines). Once a
pairing agent asks to be the default, it is asked two questions: one from an impostor, which it
must refuse, and one from BlueZ, which must wait for the user. BlueZ then withdraws it.
"""
import argparse
import json
import signal

from gi.repository import Gio, GLib

try:
    from gi.repository import GLibUnix
    signal_add = GLibUnix.signal_add
except ImportError:
    signal_add = GLib.unix_signal_add

DEVICE = "/org/bluez/hci0/dev_AA_00_00_00_00_01"
INTERFACES = Gio.DBusNodeInfo.new_for_xml("""
<node>
  <interface name="org.freedesktop.DBus.ObjectManager">
    <method name="GetManagedObjects"><arg type="a{oa{sa{sv}}}" direction="out"/></method>
  </interface>
  <interface name="org.bluez.AgentManager1">
    <method name="RegisterAgent"><arg type="o" direction="in"/><arg type="s" direction="in"/></method>
    <method name="UnregisterAgent"><arg type="o" direction="in"/></method>
    <method name="RequestDefaultAgent"><arg type="o" direction="in"/></method>
  </interface>
</node>""").interfaces
FLAGS = Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION


def managed_objects():
    adapter = {"org.bluez.Adapter1": {"Address": GLib.Variant("s", "00:11:22:33:44:55"),
                                      "Alias": GLib.Variant("s", "qualification"),
                                      "Powered": GLib.Variant("b", True),
                                      "Discovering": GLib.Variant("b", False)}}
    device = {"org.bluez.Device1": {"Address": GLib.Variant("s", "AA:00:00:00:00:01"),
                                    "Alias": GLib.Variant("s", "Qualification Headphones"),
                                    "Icon": GLib.Variant("s", "audio-headset"),
                                    "Paired": GLib.Variant("b", True),
                                    "Trusted": GLib.Variant("b", True),
                                    "Connected": GLib.Variant("b", False),
                                    "Adapter": GLib.Variant("o", "/org/bluez/hci0")},
              "org.bluez.Battery1": {"Percentage": GLib.Variant("y", 77)}}
    return GLib.Variant("(a{oa{sa{sv}}})", ({"/org/bluez": {"org.bluez.AgentManager1": {}},
                                             "/org/bluez/hci0": adapter, DEVICE: device},))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True)
    parser.add_argument("--log", required=True)
    options = parser.parse_args()
    log = open(options.log, "a", buffering=1)
    record = lambda entry: log.write(json.dumps(entry) + "\n")
    bluez = Gio.DBusConnection.new_for_address_sync(options.address, FLAGS, None, None)
    agent = {}

    def caller_pid(sender):
        reply = bluez.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                "GetConnectionUnixProcessID", GLib.Variant("(s)", (sender,)),
                                GLib.VariantType("(u)"), Gio.DBusCallFlags.NONE, 2000, None)
        return reply.unpack()[0]

    def outcome(error):
        return Gio.DBusError.get_remote_error(error) or ("timeout" if error.matches(
            Gio.io_error_quark(), Gio.IOErrorEnum.TIMED_OUT) else error.message)

    def ask():
        # Someone else on the bus cannot ask the agent anything.
        impostor = Gio.DBusConnection.new_for_address_sync(options.address, FLAGS, None, None)
        try:
            impostor.call_sync(agent["owner"], agent["path"], "org.bluez.Agent1", "RequestConfirmation",
                               GLib.Variant("(ou)", (DEVICE, 111111)), None, Gio.DBusCallFlags.NONE, 5000, None)
            record({"impostor": "answered"})
        except GLib.Error as error:
            record({"impostor": outcome(error)})
        impostor.close_sync(None)

        def withdrawn(connection, result):
            try:
                connection.call_finish(result)
                record({"cancel": "ok"})
            except GLib.Error as error:
                record({"cancel": outcome(error)})

        def confirmed(connection, result):
            try:
                connection.call_finish(result)
                record({"confirmation": "answered"})
            except GLib.Error as error:
                record({"confirmation": outcome(error)})
            # BlueZ withdraws a question it has stopped waiting for.
            connection.call(agent["owner"], agent["path"], "org.bluez.Agent1", "Cancel", None, None,
                            Gio.DBusCallFlags.NONE, 5000, None, withdrawn)

        # The question waits on screen for the user: no answer within three seconds.
        bluez.call(agent["owner"], agent["path"], "org.bluez.Agent1", "RequestConfirmation",
                   GLib.Variant("(ou)", (DEVICE, 123456)), None, Gio.DBusCallFlags.NONE, 3000, None, confirmed)
        return False

    def method(connection, sender, path, interface, member, parameters, invocation):
        record({"call": member, "path": path, "pid": caller_pid(sender), "arguments": parameters.unpack()})
        if member == "GetManagedObjects":
            invocation.return_value(managed_objects())
            return
        if member == "RegisterAgent":
            agent.update(owner=sender, path=parameters.unpack()[0])
        elif member == "RequestDefaultAgent" and agent.get("owner") == sender:
            GLib.timeout_add(500, ask)
        invocation.return_value(None)

    for path, interface in (("/", INTERFACES[0]), ("/org/bluez", INTERFACES[1])):
        bluez.register_object(path, interface, method, None, None)
    owned = bluez.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                            "RequestName", GLib.Variant("(su)", ("org.bluez", 4)), GLib.VariantType("(u)"),
                            Gio.DBusCallFlags.NONE, 5000, None)
    assert owned.unpack()[0] == 1, "org.bluez is already owned on this bus"
    loop = GLib.MainLoop()
    signal_add(GLib.PRIORITY_DEFAULT, signal.SIGTERM, loop.quit)
    print("ready", flush=True)
    loop.run()


if __name__ == "__main__":
    main()
