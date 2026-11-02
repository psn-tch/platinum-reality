"""Typing for tests/plymouth_preview.sh: focus the only top-level X window and type each argument
through XTEST; RET presses Return."""
import sys, time
from Xlib import X, XK, display
from Xlib.ext import xtest
d = display.Display()
root = d.screen().root
windows = [w for w in root.query_tree().children if w.get_attributes().map_state == X.IsViewable]
if not windows:
    sys.exit("no window")
d.set_input_focus(windows[-1], X.RevertToParent, X.CurrentTime)
d.sync()
for word in sys.argv[1:]:
    keys = ["Return"] if word == "RET" else list(word)
    for key in keys:
        code = d.keysym_to_keycode(XK.string_to_keysym(key))
        xtest.fake_input(d, X.KeyPress, code); d.sync(); time.sleep(0.03)
        xtest.fake_input(d, X.KeyRelease, code); d.sync(); time.sleep(0.03)
print("typed", len(windows), "windows")
