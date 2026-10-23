#!/usr/bin/env python3
"""A tray application built on libappindicator3, as Steam, blueman and many others are.

Prints one event per line: ready, clicked LABEL, activated. Commands on stdin:
attention, active, passive, title TEXT, quit.
"""
import sys

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("AppIndicator3", "0.1")
from gi.repository import AppIndicator3, GLib, Gtk  # noqa: E402


def say(text):
    print(text, flush=True)


indicator = AppIndicator3.Indicator.new(
    "platinum-status-probe", "dialog-information", AppIndicator3.IndicatorCategory.APPLICATION_STATUS)
indicator.set_title("Status Probe")
indicator.set_attention_icon_full("dialog-warning", "Needs attention")
menu = Gtk.Menu()
for label in ("Show Library", "Check for Updates"):
    item = Gtk.MenuItem(label=label)
    item.connect("activate", lambda widget, text=label: say("clicked " + text))
    menu.append(item)
menu.append(Gtk.SeparatorMenuItem())
toggle = Gtk.CheckMenuItem(label="Do Not Disturb")
toggle.set_active(True)
toggle.connect("toggled", lambda widget: say("toggled " + str(widget.get_active())))
menu.append(toggle)
disabled = Gtk.MenuItem(label="Unavailable Item")
disabled.set_sensitive(False)
menu.append(disabled)
quit_item = Gtk.MenuItem(label="Quit Probe")
quit_item.connect("activate", lambda widget: (say("clicked Quit Probe"), Gtk.main_quit()))
menu.append(quit_item)
menu.show_all()
indicator.set_menu(menu)
indicator.set_secondary_activate_target(menu.get_children()[0])
indicator.set_status(AppIndicator3.IndicatorStatus.ACTIVE)


def command(channel, condition):
    line = channel.readline()
    if not line:
        Gtk.main_quit()
        return False
    line = line.strip()
    if line == "quit":
        Gtk.main_quit()
    elif line == "attention":
        indicator.set_status(AppIndicator3.IndicatorStatus.ATTENTION)
        say("status attention")
    elif line == "active":
        indicator.set_status(AppIndicator3.IndicatorStatus.ACTIVE)
        say("status active")
    elif line == "passive":
        indicator.set_status(AppIndicator3.IndicatorStatus.PASSIVE)
        say("status passive")
    elif line.startswith("title "):
        indicator.set_title(line[6:])
        say("title set")
    return True


GLib.io_add_watch(GLib.IOChannel.unix_new(sys.stdin.fileno()), GLib.IO_IN | GLib.IO_HUP, command)
GLib.idle_add(lambda: say("ready") or False)
Gtk.main()
