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

python3 - "$runtime" "$repo" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
sys.path.insert(0, str(pathlib.Path(sys.argv[2]) / "tests"))
from validate_png import decode_png

expected = ["final", "albedo", "normal", "ao", "diffuse", "specular", "shadow"]
found = []
by_mode = {}
for report in sorted((root / "screenshots").glob("*.txt")):
    fields = {}
    for line in report.read_text().splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key] = value
    if fields.get("format.version") != "14":
        raise SystemExit(f"{report}: unexpected format version {fields.get('format.version')!r}")
    mode = fields.get("render.debug_mode")
    found.append(mode)
    by_mode[mode] = report
if found != expected:
    raise SystemExit(f"render debug mode reports differ: expected={expected} actual={found}")

def center_mean(report):
    width, height, bytes_per_pixel, rows = decode_png(report.with_suffix(".png"))
    sums = [0, 0, 0]
    count = 0
    for y in range(height * 25 // 100, height * 70 // 100):
        row = rows[y]
        for x in range(width * 15 // 100, width * 85 // 100):
            offset = x * bytes_per_pixel
            for channel in range(3):
                sums[channel] += row[offset + channel]
            count += 1
    return tuple(value / count for value in sums)

normal_mean = center_mean(by_mode["normal"])
if normal_mean[1] < normal_mean[0] + 40 or normal_mean[1] < normal_mean[2] + 40:
    raise SystemExit(
        "normal debug view does not show upward-facing terrain: "
        f"mean={tuple(round(value, 1) for value in normal_mean)}")
diffuse_mean = center_mean(by_mode["diffuse"])
if max(diffuse_mean) < 24:
    raise SystemExit(
        "diffuse debug view is black; material/normal sampler binding regressed: "
        f"mean={tuple(round(value, 1) for value in diffuse_mean)}")

width, height, bytes_per_pixel, rows = decode_png(
    by_mode["shadow"].with_suffix(".png"))
# The 3x4 stone wall at z=8 casts a shadow to the right of the wall at the
# pinned 0.375 day phase. Sample that ground patch directly so an all-white or
# all-dark shadow texture cannot satisfy the check accidentally.
lit_pixels = 0
shadowed_pixels = 0
sampled_pixels = 0
for y in range(height * 50 // 100, height * 62 // 100):
    row = rows[y]
    for x in range(width * 55 // 100, width * 80 // 100):
        offset = x * bytes_per_pixel
        luminance = (0.2126*row[offset] + 0.7152*row[offset + 1] +
                     0.0722*row[offset + 2])
        lit_pixels += luminance >= 220.0
        shadowed_pixels += luminance < 160.0
        sampled_pixels += 1
lit_fraction = lit_pixels / sampled_pixels
shadow_fraction = shadowed_pixels / sampled_pixels
if lit_fraction < 0.50 or shadow_fraction < 0.05:
    raise SystemExit(
        "shadow debug view does not show the expected wall cast shadow: "
        f"lit_fraction={lit_fraction:.3f} shadow_fraction={shadow_fraction:.3f}")
print("render debug mode report and lighting image checks passed")
PY

for image in "${images[@]}"; do
    python3 "$repo/tests/validate_png.py" "$image" 1280 720 --allow-dark-ui
done

printf '%s\n' 'render debug modes e2e passed'
