#!/usr/bin/env bash
#
# Capture every world-surface render debug mode and verify that each screenshot
# report carries the selected mode. This requires a real Hyprland/Wayland frame
# loop because the modes are fragment-shader branches and are intentionally
# absent from the CPU-only renderer tests.

set -euo pipefail

binary=$(realpath "${1:-build/normal/voxelcraft}")
repo=$(cd "$(dirname "$0")/.." && pwd)

if [[ ! -x "$binary" ]]; then
    echo "game binary is not executable: $binary" >&2
    exit 2
fi

if [[ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" ||
      -z "${WAYLAND_DISPLAY:-}" || -z "${XDG_RUNTIME_DIR:-}" ||
      ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
    echo 'an active Wayland session is required' >&2
    exit 2
fi

runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-render-debug.XXXXXX")
ln -s "$repo/assets" "$runtime/assets"
cp "$repo/tests/scenes/render_debug_modes.dsl" "$runtime/scene.dsl"

set +e
(cd "$runtime" && timeout 300 "$binary" --debug-resolution 1280x720 \
    --debug-script "$runtime/scene.dsl" \
    --debug-trace "$runtime/trace.jsonl") >"$runtime/game.log" 2>&1
status=$?
set -e

printf 'render debug modes artifacts: %s\n' "$runtime"
if ((status != 0)); then
    tail -80 "$runtime/game.log" >&2
    exit "$status"
fi

grep -q '^DEBUG_SCRIPT exit code=0$' "$runtime/game.log"
reports=("$runtime"/screenshots/*.txt)
images=("$runtime"/screenshots/*.png)
(( ${#reports[@]} == 7 ))
(( ${#images[@]} == 7 ))

python3 - "$runtime" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
expected = ["final", "albedo", "normal", "ao", "diffuse", "specular", "shadow"]
found = []
for report in sorted((root / "screenshots").glob("*.txt")):
    fields = {}
    for line in report.read_text().splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key] = value
    if fields.get("format.version") != "14":
        raise SystemExit(f"{report}: unexpected format version {fields.get('format.version')!r}")
    found.append(fields.get("render.debug_mode"))
if found != expected:
    raise SystemExit(f"render debug mode reports differ: expected={expected} actual={found}")
print("render debug mode report sequence passed")
PY

for image in "${images[@]}"; do
    python3 "$repo/tests/validate_png.py" "$image" 1280 720 --allow-dark-ui
done

printf '%s\n' 'render debug modes e2e passed'
