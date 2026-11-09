#!/usr/bin/env bash
# Runs the Keychain module's test against a throwaway, unlocked GNOME Keyring on a private bus.
set -euo pipefail
build="${1:-$(dirname "$0")/../build}"
home="$(mktemp -d)"
trap 'rm -rf -- "$home"' EXIT
env -i HOME="$home" PATH=/usr/bin:/bin LANG=C.UTF-8 QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR="$home" \
    PLATINUM_THROWAWAY_KEYRING=1 \
    dbus-run-session -- bash -c '
        printf test | gnome-keyring-daemon --unlock --components=secrets >/dev/null
        exec "$0/desktop-tests" keychainModuleLocksKeychainsByName' "$(cd "$build" && pwd)"
