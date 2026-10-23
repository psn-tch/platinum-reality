#!/usr/bin/env python3
"""An application whose tray item and menus never answer a click, as a hung one does.

It exports a status item with a dbusmenu menu, and the same menu at /MenuBar as an application
menu. Properties, GetLayout and AboutToShow are answered; Event and Activate never are. It counts
how many of those arrive, and how many arrive marked no-reply-expected.

Prints "ready UNIQUE-NAME PID". On stdin, "report" prints
"report EVENTS NO-REPLY-EVENTS ACTIVATIONS NO-REPLY-ACTIVATIONS"; end of input quits.
"""
import os
import sys

import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

ITEM = "org.kde.StatusNotifierItem"
MENU = "com.canonical.dbusmenu"
counts = {"event": [0, 0], "activate": [0, 0]}


def record(kind, message):
    counts[kind][0] += 1
    if message.get_no_reply():
        counts[kind][1] += 1


def entry(identity, properties, children=()):
    return dbus.Struct((dbus.Int32(identity), dbus.Dictionary(properties, signature="sv"),
                        dbus.Array(children, signature="v")), signature="ia{sv}av")


class Item(dbus.service.Object):
    @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="s", out_signature="a{sv}")
    def GetAll(self, interface):
        return {"Id": "platinum-silent-probe", "Title": "Silent Probe", "Status": "Active",
                "IconName": "dialog-information", "ItemIsMenu": False, "Menu": dbus.ObjectPath("/Menu")}

    @dbus.service.method(ITEM, in_signature="ii", message_keyword="message", async_callbacks=("ok", "fail"))
    def Activate(self, x, y, message, ok, fail):
        record("activate", message)


class Menu(dbus.service.Object):
    @dbus.service.method(MENU, in_signature="iias", out_signature="u(ia{sv}av)")
    def GetLayout(self, parent, depth, names):
        item = entry(2, {"label": "Silent Item"})
        return dbus.UInt32(1), entry(0, {}, [entry(1, {"label": "Silent", "children-display": "submenu"}, [item])])

    @dbus.service.method(MENU, in_signature="i", out_signature="b")
    def AboutToShow(self, identity):
        return False

    @dbus.service.method(MENU, in_signature="isvu", message_keyword="message", async_callbacks=("ok", "fail"))
    def Event(self, identity, kind, data, timestamp, message, ok, fail):
        record("event", message)


def command(channel, condition):
    line = channel.readline()
    if not line:
        loop.quit()
        return False
    if line.strip() == "report":
        print("report", *counts["event"], *counts["activate"], flush=True)
    return True


dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
Item(bus, "/StatusNotifierItem")
Menu(bus, "/Menu")
Menu(bus, "/MenuBar")
bus.get_object("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher").RegisterStatusNotifierItem(
    "/StatusNotifierItem", dbus_interface="org.kde.StatusNotifierWatcher")
loop = GLib.MainLoop()
GLib.io_add_watch(GLib.IOChannel.unix_new(sys.stdin.fileno()), GLib.IO_IN | GLib.IO_HUP, command)
print("ready", bus.get_unique_name(), os.getpid(), flush=True)
loop.run()
