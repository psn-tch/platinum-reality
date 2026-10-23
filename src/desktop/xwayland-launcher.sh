#!/bin/sh
# Platinum Reality' private Xwayland launcher, installed as xwayland/Xwayland.
# X11Bridge puts this directory first on xwayland-satellite's PATH, so only the
# desktop's own Xwayland starts here. See xwayland_implicit_sync.c for why.
here=${0%/*}
shim=$here/libplatinum-xwayland-implicit-sync.so
case $shim in
    /*) ;;
    *) echo "Platinum Reality: Xwayland launcher needs an absolute path; X11 Vulkan clients may hang if the desktop exits" >&2
       exec /usr/bin/Xwayland "$@" ;;
esac
case $shim in
    *[[:space:]:]*)
        # LD_PRELOAD splits on spaces and colons.
        echo "Platinum Reality: installation path unusable for LD_PRELOAD; X11 Vulkan clients may hang if the desktop exits" >&2
        exec /usr/bin/Xwayland "$@" ;;
esac
if [ ! -f "$shim" ]; then
    echo "Platinum Reality: $shim is missing; X11 Vulkan clients may hang if the desktop exits" >&2
    exec /usr/bin/Xwayland "$@"
fi
LD_PRELOAD=$shim exec /usr/bin/Xwayland "$@"
