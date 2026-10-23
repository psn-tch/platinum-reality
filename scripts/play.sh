#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
for program in frankenstein native-tests frankenstein-finder frankenstein-editor frankenstein-drop-receiver frankenstein-puzzle; do
    if [[ ! -x "$project_dir/build/$program" ]]; then
        printf 'Missing build/%s. Build the project before starting the play session.\n' "$program" >&2
        exit 2
    fi
done
bash "$project_dir/scripts/qualify-native.sh"
printf '\nAutomatic native checks passed. Opening Platinum Reality for hands-on use.\n'
printf 'Use the Apple menu for Applications and End Nested Session.\n'
exec bash "$project_dir/scripts/run-nested.sh" "$@"
