#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${FRANKENSTEIN_NATIVE_BUILD_DIR:-$project_dir/build}"
build_dir="$(cd -- "$build_dir" && pwd)"
# The complete daily-use gate includes X11 browsers, games and clipboard. A
# leftover helper binary does not mean this build actually compiled the bridge.
# Focused/experimental tests may still be used on an intentional Wayland-only
# build; never spend a full gate timing out against a disabled service.
# A run is complete when it names no test functions, whatever QtTest options it passes
# (-maxwarnings 0 is required for gates: the default 2000-message cap hides late diagnostics).
complete=1
option_value=0
for argument in "$@"; do
    if (( option_value )); then option_value=0; continue; fi
    case "$argument" in
        -maxwarnings|-o|-eventdelay|-keydelay|-mousedelay|-seed|-repeat|-platform) option_value=1 ;;
        -*) ;;
        *) complete=0 ;;
    esac
done
if (( complete )) && [[ "${FRANKENSTEIN_MULTI_DISPLAY_EXPERIMENT:-0}" != 1 ]] &&
   ! rg -q '^PLATINUM_XWAYLAND_SATELLITE:FILEPATH=.+' "$build_dir/CMakeCache.txt"; then
    echo 'Full native qualification requires the provenance-recorded X11 bridge enabled in CMake. See docs/x11-integration.md.' >&2
    exit 2
fi
# A hang guard only: Wine bootstraps a new prefix inside one test (about 21 s observed).
default_function_timeout=60000
if rg -q -- '^CMAKE_CXX_FLAGS:STRING=.*-fsanitize=[^ ]*address' "$build_dir/CMakeCache.txt"; then
    # Owned Vulkan loader startup measured 23.6 s under ASan, independently of
    # this compositor. Leave room for its bounded initialization and the actual
    # frame/interaction/teardown checks, plus Wine's prefix bootstrap. Explicit caller limits still win.
    default_function_timeout=90000
fi
# External, uninstrumented browsers may load Qt's style shim. Keep their plugin
# uninstrumented while testing the shell and owned Qt clients with sanitizers.
if [[ -z "${FRANKENSTEIN_EXTERNAL_STYLE_DIRECTORY:-}" ]] &&
   rg -q -- '^CMAKE_CXX_FLAGS:STRING=.*-fsanitize=[^ ]*address' "$build_dir/CMakeCache.txt"; then
    export FRANKENSTEIN_EXTERNAL_STYLE_DIRECTORY="$project_dir/build/plugins"
    if [[ ! -f "$FRANKENSTEIN_EXTERNAL_STYLE_DIRECTORY/styles/libfrankenstein-style.so" ]]; then
        echo 'Build the matching normal style plugin before external-browser sanitizer qualification.' >&2
        exit 1
    fi
fi
mkdir -p -- "$project_dir/build/artifacts"
run_dir="$project_dir/build/artifacts/native-runs/$(date -u +%Y%m%dT%H%M%S)-$$"
mkdir -p -- "$run_dir"
python3 "$project_dir/scripts/check-ipc.py" 2>&1 | tee "$run_dir/preflight.txt"
if [[ -z "${WAYLAND_DISPLAY:-}" && -z "${DISPLAY:-}" ]]; then
    echo 'Native qualification requires an existing graphical session for its nested window.' >&2
    exit 77
fi
if [[ -n "${WAYLAND_DISPLAY:-}" && "${WAYLAND_DISPLAY}" != /* ]]; then
    export WAYLAND_DISPLAY="${XDG_RUNTIME_DIR:?No parent runtime directory}/${WAYLAND_DISPLAY}"
fi
runtime_dir="$(mktemp -d /tmp/frankenstein-runtime.XXXXXX)"
cleanup() {
    local result=$?
    trap - EXIT
    local cleanup_status=0
    python3 "$project_dir/scripts/session_support.py" cleanup "$runtime_dir" > "$run_dir/cleanup.txt" 2>&1 || cleanup_status=$?
    printf '%s\n' "$cleanup_status" > "$run_dir/cleanup-exit-status.txt"
    cat "$run_dir/cleanup.txt"
    if (( result == 0 && cleanup_status != 0 )); then result=1; fi
    exit "$result"
}
trap cleanup EXIT
export XDG_RUNTIME_DIR="$runtime_dir"
export FRANKENSTEIN_NATIVE_TEST=1
export QSG_RHI_BACKEND=opengl
export QTEST_FUNCTION_TIMEOUT="${QTEST_FUNCTION_TIMEOUT:-$default_function_timeout}"
export FRANKENSTEIN_EVIDENCE_DIR="$run_dir"
python3 "$project_dir/scripts/record-run.py" "$project_dir" "$run_dir" "$build_dir"
python3 "$project_dir/scripts/session_support.py" configure "$runtime_dir" > /dev/null
test_status=0
test_binary=native-tests
if [[ "${FRANKENSTEIN_MULTI_DISPLAY_EXPERIMENT:-0}" == 1 ]]; then test_binary=multi-display-tests; fi
dbus-run-session --config-file="$runtime_dir/bus.conf" -- "$build_dir/$test_binary" -o "$run_dir/native-tests.txt,txt" "$@" 2> "$run_dir/stderr.txt" || test_status=$?
if [[ -s "$run_dir/stderr.txt" ]]; then cat "$run_dir/stderr.txt" >&2; fi
if [[ -f "$run_dir/native-tests.txt" ]]; then cat "$run_dir/native-tests.txt"; fi
printf '%s\n' "$test_status" > "$run_dir/exit-status.txt"
echo "Native test result: $test_status. Evidence: $run_dir"
exit "$test_status"
