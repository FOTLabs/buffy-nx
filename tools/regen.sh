#!/usr/bin/env bash
# Regenerate Buffy's lifted C: [disasm -> func_id -> abi ->] xdk hooks -> recomp.
# Output goes to a temp dir and only changed files are copied into GEN, so a
# hook change rebuilds the one chunk it touches instead of all of them.
#   LIFT_ONLY=1   skip disasm/func_id/abi (they only depend on the XBE + seeds)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TK="${XBOXRECOMP_DIR:-$HERE/sdk}"
XBE="${GAME_XBE:-$HERE/game/Default.xbe}"
mkdir -p "$HERE/game"
GEN="${GAME_GEN_DIR:-$HERE/app/generated/recomp}"
PY="${PYTHON:-python3}"
cd "$TK"
"$PY" -m tools.xbe_parser "$XBE" --json "$HERE/game/Default_analysis.json" >/dev/null
if [ "${LIFT_ONLY:-0}" != "1" ]; then
    "$PY" -m tools.disasm "$XBE" --seed-functions "$HERE/app/config/seed_functions.json"
    "$PY" -m tools.func_id "$XBE"
    "$PY" -m tools.abi_analysis "$XBE"
fi
"$PY" "$HERE/tools/map_prune.py" tools/disasm/output/functions.json "$HERE/app/config/map_names.json"
"$PY" "$HERE/tools/map_extend.py" tools/disasm/output/functions.json "$HERE/app/config/map_names.json" "$XBE" "$HERE/game/Default_analysis.json"
"$PY" -m tools.xdk_hooks --names "$HERE/app/config/map_names.json" \
    --functions tools/disasm/output/functions.json --analysis "$HERE/game/Default_analysis.json" \
    --enable "$(grep -v '^#' "$HERE/app/config/xdk_hooks.txt" | paste -sd, -)" --out-dir "$HERE/app/generated/hooks"
TMP="$GEN.new"; rm -rf "$TMP"; mkdir -p "$TMP" "$GEN"
RECOMP_CHUNK_BYTES=${RECOMP_CHUNK_BYTES:-0x10000} "$PY" -m tools.recomp "$XBE" --game-name "Buffy the Vampire Slayer" --all --split 250 \
    --gen-dir "$TMP" --exclude-manual "$HERE/app/src/recomp_manual.c,$HERE/app/src/buffy_overrides.c,$HERE/app/generated/hooks/xdk_hooks_glue.c" \
    --mmio-sections DSOUND,XPP
"$PY" "$HERE/tools/localize_math.py" "$TMP"
n=0
for f in "$TMP"/*; do
    b=$(basename "$f")
    if ! cmp -s "$f" "$GEN/$b"; then
        # A header whose only change is added declarations (a wrapped sub_X_gen
        # prototype) cannot change what any compiled chunk means: keep its old
        # time stamp, or every chunk that includes it rebuilds (~40 min).
        keep=0
        if [ -e "$GEN/$b" ] && [ "${b##*.}" = h ] &&            ! diff "$GEN/$b" "$f" | grep -v '^[0-9,]*a[0-9,]*$' | grep -qv '^> void [A-Za-z0-9_]*(void);'; then
            keep=1; touch -r "$GEN/$b" "$f"
        fi
        cp -p "$f" "$GEN/$b"; n=$((n+1)); echo "  changed: $b$([ $keep = 1 ] && echo ' (declarations only, time stamp kept)')"
    fi
done
for f in "$GEN"/*; do [ -e "$TMP/$(basename "$f")" ] || { rm "$f"; echo "  removed: $(basename "$f")"; }; done
rm -rf "$TMP"
echo "regen: $n files changed in $GEN"
