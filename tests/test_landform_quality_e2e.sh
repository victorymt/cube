#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
binary=$(realpath "${1:-build/normal/voxelcraft}")
if [[ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" || -z "${WAYLAND_DISPLAY:-}" ||
      -z "${XDG_RUNTIME_DIR:-}" || ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
    echo 'an active Hyprland/Wayland session is required' >&2
    exit 2
fi
runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-landform-quality.XXXXXX")
ln -s "$repo/assets" "$runtime/assets"
cp "$repo/tests/scenes/landform_quality.dsl" "$runtime/check.dsl"
printf 'landform quality artifacts: %s\n' "$runtime"
(cd "$runtime" && timeout "${LANDFORM_QUALITY_TIMEOUT:-360}" env -u LIBGL_ALWAYS_SOFTWARE \
    "$binary" --debug-script "$runtime/check.dsl" --debug-resolution 1280x720 \
    --debug-trace "$runtime/trace.jsonl") > "$runtime/game.log" 2>&1 || {
    tail -40 "$runtime/game.log" >&2
    exit 1
}
grep -q '^DEBUG_SCRIPT exit code=0$' "$runtime/game.log"
uv run --no-project --offline python - "$runtime" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
images = sorted((root / 'screenshots').glob('*.png'))
labels = ['1234 mouth', '1234 lake', '4321 mouth', '4321 lake', '991 mouth', '991 lake',
          '991 date line', '991 cube face edge', '991 north pole', '991 south pole',
          '991 desert north pole control', '991 desert south pole control']
assert len(images) == len(labels), (len(images), len(labels))
rows = ['# Planet landform visual review', '', 'Automated replay passed; inspect each image for terrain/water continuity.', '']
for label, image in zip(labels, images):
    report = image.with_suffix('.txt')
    assert report.exists(), report
    fields = dict(line.split('=', 1) for line in report.read_text().splitlines() if '=' in line)
    assert fields['capture.id'] == fields['chunks.capture_id'], report
    assert int(fields['capture.simulation_tick']) >= 180, report
    assert fields['camera.inside_solid'] == 'false', report
    assert fields['streaming.surface_ready'] == 'true', report
    rows.extend([f'## {label}', '', f'![{label}]({image.relative_to(root)})', '', f'Report: {report.relative_to(root)}', ''])
(root / 'review.md').write_text('\n'.join(rows))
print(f'Captured {len(images)} views; review index: {root / "review.md"}')
PY
printf '%s\n' 'landform quality capture/DSL checks passed; visual acceptance requires reviewing screenshots'
