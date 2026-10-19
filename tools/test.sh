#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
TMP="$(mktemp -d)"
trap 'rm -f "$TMP/visibility" "$TMP/rumble" "$TMP/aim"; rmdir "$TMP"' EXIT
for name in visibility rumble aim_input; do
    output="$name"; [ "$name" != aim_input ] || output=aim
    cc -O2 "tests/test_$name.c" -pthread -lm -o "$TMP/$output"
    "$TMP/$output"
done
python3 tests/test_pause_image.py
python3 tests/test_pause_transition.py
"${NODE:-node}" --test installer/tests/*.test.mjs
