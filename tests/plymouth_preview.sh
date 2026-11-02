#!/usr/bin/env bash
# The boot screen's unlock dialog in a real plymouthd, without a reboot (docs/boot-splash.md).
# Run ONLY inside scripts/with-headless-display.py:
#   python3 scripts/with-headless-display.py --scale 1.0 --evidence DIR/harness -- \
#       bash tests/plymouth_preview.sh DIR/out [SIZE [QUARTER_TURNS]]
# It renders and installs the theme into a scratch root, starts a private rootful Xwayland on the
# headless display, and runs plymouthd under bubblewrap as a namespace-only "root" with a private
# /dev: no console, graphics card or input device of the machine is reachable (plymouthd opened the
# real /dev/tty1 and /dev/dri/card1 when only / was bound, 2026-10-30). It then asks for a password
# twice, types through XTEST, and saves screenshots and the passwords plymouthd returned.
set -euo pipefail
[[ -n ${FRANKENSTEIN_HEADLESS_IPC:-} ]] || { echo 'Run this inside scripts/with-headless-display.py.' >&2; exit 2; }
repo=$(cd "$(dirname "$0")/.." && pwd) out=$1 size=${2:-1120x760} turns=${3:-0}
work=$(mktemp -d)
trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$work"' EXIT
mkdir -p "$work"/{home,rendered,root/usr/bin,root/etc/kernel,root/etc/mkinitcpio.conf.d,etc-plymouth,run} "$out"
touch "$work/root/usr/bin/plymouth"
echo 'root=x rw' > "$work/root/etc/kernel/cmdline"
echo 'HOOKS=(base udev autodetect kms block filesystems)' > "$work/root/etc/mkinitcpio.conf"
HOME=$work/home python3 -B "$repo/scripts/boot-splash.py" render --output "$work/rendered" --size "$size" \
    --rotate "$turns" --cmdline /dev/null
HOME=$work/home python3 -B "$repo/scripts/boot-splash.py" install --from "$work/rendered" --root "$work/root" \
    --no-rebuild > /dev/null
printf '[Daemon]\nTheme=platinum-reality\n' > "$work/etc-plymouth/plymouthd.conf"
cp "$repo/tests/plymouth_xtype.py" "$work/xtype.py"
cat > "$work/inside.sh" <<'EOF'
set -u
out=$1 type="python3 -I $2"
for device in /dev/tty0 /dev/tty1 /dev/console /dev/dri /dev/input /dev/fb0; do
    if [ -e "$device" ]; then echo "UNSAFE: $device is visible in the sandbox" >&2; exit 3; fi
done
GDK_BACKEND=x11 plymouthd --no-daemon --debug --debug-file="$out/plymouthd.log" \
    --kernel-command-line="splash plymouth.ignore-udev" --mode=boot &
daemon=$!
for i in $(seq 50); do plymouth --ping 2>/dev/null && break; sleep 0.1; done
plymouth show-splash
sleep 2; grim "$out/1-splash.png"
plymouth ask-for-password --prompt="Please enter passphrase for disk root" > "$out/password-1.txt" &
sleep 1.5; grim "$out/2-prompt.png"
$type secret >> "$out/typing.log" 2>&1; sleep 1; grim "$out/3-typed.png"
$type RET >> "$out/typing.log" 2>&1; sleep 1; grim "$out/4-after.png"
plymouth ask-for-password --prompt="Please enter passphrase for disk root" > "$out/password-2.txt" &
sleep 1.5; $type ab >> "$out/typing.log" 2>&1; sleep 1; grim "$out/5-retry.png"
$type RET >> "$out/typing.log" 2>&1; sleep 0.5
plymouth quit; wait $daemon
EOF
display=$((200 + RANDOM % 500))
Xwayland ":$display" -nolisten tcp > "$out/xwayland.log" 2>&1 &
for i in $(seq 50); do [ -S "/tmp/.X11-unix/X$display" ] && break; sleep 0.1; done
bwrap --ro-bind / / --dev /dev --proc /proc --tmpfs /tmp --ro-bind /tmp/.X11-unix /tmp/.X11-unix \
    --bind "$work" "$work" --bind "$out" "$out" --bind "$XDG_RUNTIME_DIR" "$XDG_RUNTIME_DIR" \
    --ro-bind "$work/root/usr/share/plymouth/themes" /usr/share/plymouth/themes \
    --ro-bind "$work/etc-plymouth" /etc/plymouth --bind "$work/run" /run/plymouth \
    --tmpfs /sys/class/drm --tmpfs /sys/class/tty --tmpfs /sys/class/input --tmpfs /run/udev \
    --unshare-user --uid 0 --gid 0 --unshare-net --unshare-pid --unshare-ipc --die-with-parent \
    env DISPLAY=":$display" bash "$work/inside.sh" "$out" "$work/xtype.py"
[[ $(cat "$out/password-1.txt") == secret && $(cat "$out/password-2.txt") == ab ]] \
    || { echo 'FAIL: plymouthd did not return the typed passwords' >&2; exit 1; }
echo "PASS: unlock dialog preview in $out"
