#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
python3 "$project_dir/scripts/check-ipc.py"
mkdir -p -- "$project_dir/var/profile" "$project_dir/var/workspace"
if [[ -n "${WAYLAND_DISPLAY:-}" && "${WAYLAND_DISPLAY}" != /* ]]; then
    export WAYLAND_DISPLAY="${XDG_RUNTIME_DIR:?No parent runtime directory}/${WAYLAND_DISPLAY}"
fi
runtime_dir="$(mktemp -d /tmp/frankenstein-runtime.XXXXXX)"
cleanup() {
    local result=$?
    trap - EXIT
    local cleanup_status=0
    python3 "$project_dir/scripts/session_support.py" cleanup "$runtime_dir" || cleanup_status=$?
    if (( result == 0 && cleanup_status != 0 )); then result=1; fi
    exit "$result"
}
trap cleanup EXIT
python3 "$project_dir/scripts/session_support.py" configure "$runtime_dir" > /dev/null
export XDG_RUNTIME_DIR="$runtime_dir"
export FRANKENSTEIN_ISOLATED=1
dbus-run-session --config-file="$runtime_dir/bus.conf" -- "$project_dir/build/frankenstein" --workspace "$project_dir/var/workspace" --profile "$project_dir/var/profile" "$@"
