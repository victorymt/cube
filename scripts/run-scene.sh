#!/usr/bin/env bash
#
# Run one Voxelcraft debug-DSL scene in an isolated runtime directory.
#
# The game resolves saves, settings, screenshots and traces relative to its
# working directory. Running it from the repository root would overwrite the
# user's `voxelcraft_save.txt`, `voxelcraft_settings.cfg` and screenshots.
# This runner therefore always executes from a separate directory that only
# links the repository assets.

set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
binary="$repo/build/normal/voxelcraft"
resolution="1280x720"
timeout_seconds=180
run_dir=""
append_dsl=""
trace=1
interactive=0
force=0
scene_file=""

usage() {
    cat <<'EOF'
Usage: scripts/run-scene.sh [options] <scene>

Run a Voxelcraft debug-DSL scene in an isolated runtime directory.

Arguments:
  <scene>                 Scene name under tests/scenes, or a path to a .dsl file.

Options:
  --binary PATH           Game binary (default: build/normal/voxelcraft).
  --resolution WxH        Window size (default: 1280x720, ranges 320-7680 x 240-4320).
  --timeout SECONDS       Wall-clock timeout (default: 180, range 1-3600).
  --run-dir DIR           Artifact directory (default: mktemp -d under $TMPDIR).
  --scene-file PATH       Run a .dsl file from anywhere instead of tests/scenes.
                          Use this for throwaway reproductions and generated
                          probe scenes; the file is copied, not modified.
  --append-dsl TEXT       Extra DSL lines appended after the scene.
  --no-trace              Do not write trace.jsonl.
  --interactive           Do not append `exit 0`; leave the window open.
  --force                 Allow writing into a non-empty --run-dir.
  -h, --help              Show this help.

The script always runs the game in <run-dir> with an `assets` symlink, so it
never overwrites the user's save, settings, or screenshots. It requires an
active Hyprland/Wayland session: WAYLAND_DISPLAY must name a socket below
XDG_RUNTIME_DIR. Artifacts (manifest.txt, game.log, scene.dsl, screenshots/,
trace.jsonl) stay in <run-dir>.
EOF
}

die() {
    printf 'run-scene: %s\n' "$*" >&2
    exit 2
}

while (($#)); do
    case "$1" in
        --binary)
            [[ $# -ge 2 ]] || die "--binary requires a value"
            binary=$2
            shift 2
            ;;
        --resolution)
            [[ $# -ge 2 ]] || die "--resolution requires a value"
            resolution=$2
            shift 2
            ;;
        --timeout)
            [[ $# -ge 2 ]] || die "--timeout requires a value"
            timeout_seconds=$2
            shift 2
            ;;
        --run-dir)
            [[ $# -ge 2 ]] || die "--run-dir requires a value"
            run_dir=$2
            shift 2
            ;;
        --append-dsl)
            [[ $# -ge 2 ]] || die "--append-dsl requires a value"
            append_dsl=$2
            shift 2
            ;;
        --scene-file)
            [[ $# -ge 2 ]] || die "--scene-file requires a value"
            scene_file=$2
            shift 2
            ;;
        --no-trace)
            trace=0
            shift
            ;;
        --interactive)
            interactive=1
            shift
            ;;
        --force)
            force=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        -*)
            die "unknown option: $1 (use --help)"
            ;;
        *)
            break
            ;;
    esac
done

if [[ -n "$scene_file" ]]; then
    [[ $# -le 1 ]] || die "expected at most one scene argument with --scene-file"
    [[ -f "$scene_file" ]] || die "scene file not found: $scene_file"
    scene_path=$(realpath "$scene_file")
else
    [[ $# -eq 1 ]] || die "expected exactly one scene argument (use --help)"
    scene_arg=$1
fi

command -v timeout >/dev/null 2>&1 || die "the timeout command is required"

[[ "$resolution" =~ ^([0-9]+)x([0-9]+)$ ]] ||
    die "invalid --resolution: $resolution (expected WxH)"
width=${BASH_REMATCH[1]}
height=${BASH_REMATCH[2]}
((width >= 320 && width <= 7680 && height >= 240 && height <= 4320)) ||
    die "resolution out of range: $resolution (320-7680 x 240-4320)"

[[ "$timeout_seconds" =~ ^[0-9]+$ ]] ||
    die "invalid --timeout: $timeout_seconds (expected seconds)"
((timeout_seconds >= 1 && timeout_seconds <= 3600)) ||
    die "timeout out of range: $timeout_seconds (1-3600)"

if [[ "$binary" != /* ]]; then
    binary="$repo/$binary"
fi
[[ -x "$binary" ]] ||
    die "game binary is not executable: $binary (run make first)"

[[ -n "${WAYLAND_DISPLAY:-}" ]] ||
    die "WAYLAND_DISPLAY is not set; run from an active Hyprland/Wayland session"
[[ -n "${XDG_RUNTIME_DIR:-}" ]] ||
    die "XDG_RUNTIME_DIR is not set; run from an active Hyprland/Wayland session"

if [[ "$WAYLAND_DISPLAY" == /* ]]; then
    wayland_socket=$WAYLAND_DISPLAY
else
    wayland_socket="$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY"
fi
[[ -S "$wayland_socket" ]] ||
    die "Wayland socket not found: $wayland_socket"

if [[ -n "$scene_file" ]]; then
    scenes_dir=$(realpath "$repo/tests/scenes")
    case "$scene_path" in
        *.dsl) ;;
        *) die "scene file must end in .dsl: $scene_file" ;;
    esac
else
    if [[ -f "$repo/tests/scenes/$scene_arg" ]]; then
        scene_path=$(realpath "$repo/tests/scenes/$scene_arg")
    elif [[ -f "$repo/tests/scenes/$scene_arg.dsl" ]]; then
        scene_path=$(realpath "$repo/tests/scenes/$scene_arg.dsl")
    elif [[ "$scene_arg" == */* ]]; then
        scene_path=$(realpath "$scene_arg" 2>/dev/null) ||
            die "scene not found: $scene_arg"
    else
        die "scene not found under $repo/tests/scenes: $scene_arg"
    fi

    scenes_dir=$(realpath "$repo/tests/scenes")
    case "$scene_path" in
        "$scenes_dir"/*.dsl) ;;
        *) die "scene must be a .dsl file under $scenes_dir: $scene_arg" ;;
    esac
fi

if [[ -z "$run_dir" ]]; then
    run_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-scene.XXXXXX")
else
    run_dir=$(realpath -m "$run_dir")
    if [[ -e "$run_dir" && ! -d "$run_dir" ]]; then
        die "run dir exists and is not a directory: $run_dir"
    fi
    if [[ -d "$run_dir" && -n "$(ls -A "$run_dir" 2>/dev/null)" && $force -eq 0 ]]; then
        die "run dir is not empty: $run_dir (use --force)"
    fi
    mkdir -p "$run_dir"
fi

if [[ -e "$run_dir/assets" || -L "$run_dir/assets" ]]; then
    if ((force)); then
        rm -f "$run_dir/assets"
    else
        die "run dir already has an assets entry: $run_dir"
    fi
fi
ln -s "$repo/assets" "$run_dir/assets"

cp "$scene_path" "$run_dir/scene.dsl"
if [[ -n "$append_dsl" ]]; then
    printf '%s\n' "$append_dsl" >>"$run_dir/scene.dsl"
fi
# A scene may already own its exit statement, and `exit` is only valid as the
# final top-level statement. Appending a second one turns a passing scene into
# a syntax error, so only add an exit when the scene does not end with one.
scene_ends_with_exit=0
last_statement=$(grep -v '^[[:space:]]*$' "$run_dir/scene.dsl" | grep -v '^[[:space:]]*#' | tail -1 || true)
case "$last_statement" in
    exit|exit[[:space:]]*) scene_ends_with_exit=1 ;;
esac
if ((interactive == 0 && scene_ends_with_exit == 0)); then
    printf '\nexit 0\n' >>"$run_dir/scene.dsl"
fi

started_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)
git_head=$(git -C "$repo" rev-parse --short=12 HEAD 2>/dev/null || printf 'unknown')
git_dirty=false
if [[ -n "$(git -C "$repo" status --porcelain --untracked-files=no 2>/dev/null)" ]]; then
    git_dirty=true
fi

trace_args=()
if ((trace)); then
    trace_args=(--debug-trace "$run_dir/trace.jsonl")
fi

set +e
(
    cd "$run_dir"
    timeout "${timeout_seconds}s" "$binary" \
        --debug-resolution "$resolution" \
        --debug-script "$run_dir/scene.dsl" \
        "${trace_args[@]}"
) >"$run_dir/game.log" 2>&1
status=$?
set -e

capture_count=$(grep -c '^DEBUG_CONTROL capture ok ' "$run_dir/game.log" 2>/dev/null || true)
capture_count=${capture_count:-0}
# `find` fails when the scene never captured anything and therefore never
# created screenshots/. Under `pipefail` that would abort the script before the
# manifest and diagnostics are written, which is exactly the failing run an
# operator needs to inspect.
report_count=$(find "$run_dir/screenshots" -maxdepth 1 -type f -name '*.txt' 2>/dev/null | wc -l | tr -d ' ' || true)
report_count=${report_count:-0}
dsl_errors=$(grep '^DEBUG_SCRIPT error' "$run_dir/game.log" 2>/dev/null | head -3 || true)
finished_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)

# A scene that never asks for a screenshot (benchmarks, history replays,
# diagnostics) legitimately produces no report. Only demand a capture when the
# scene contains a screenshot statement.
expects_capture=0
if grep -qE '^[[:space:]]*screenshot([[:space:]]|$)' "$run_dir/scene.dsl"; then
    expects_capture=1
fi

failure=""
if ((status != 0)); then
    if ((status == 124)); then
        failure="timeout after ${timeout_seconds}s"
    else
        failure="game exited with status $status"
    fi
elif [[ -n "$dsl_errors" ]]; then
    failure="DSL error"
elif ((expects_capture == 1 && capture_count == 0)); then
    failure="scene issues screenshot but produced no capture"
fi

cat >"$run_dir/manifest.txt" <<EOF
format=voxelcraft-run
format.version=2
scene=$(basename "$scene_path")
scene_path=$scene_path
binary=$binary
resolution=$resolution
timeout_seconds=$timeout_seconds
run_dir=$run_dir
status=$status
capture_count=$capture_count
report_count=$report_count
expects_capture=$expects_capture
ok=$([[ -z "$failure" ]] && printf 'true' || printf 'false')
failure=$failure
trace_path=$([[ $trace -eq 1 ]] && printf '%s' "$run_dir/trace.jsonl" || true)
started_at=$started_at
finished_at=$finished_at
git_head=$git_head
git_dirty=$git_dirty
EOF

printf 'run-scene: scene=%s status=%s captures=%s reports=%s\n' \
    "$(basename "$scene_path")" "$status" "$capture_count" "$report_count"
printf 'run-scene: run_dir=%s\n' "$run_dir"
printf 'run-scene: log=%s\n' "$run_dir/game.log"
if [[ "$trace" -eq 1 ]]; then
    printf 'run-scene: trace=%s\n' "$run_dir/trace.jsonl"
fi
if [[ "$report_count" -gt 0 ]]; then
    printf 'run-scene: reports:\n'
    find "$run_dir/screenshots" -maxdepth 1 -type f -name '*.txt' -print 2>/dev/null |
        sort | sed 's/^/  /'
fi

if [[ -n "$failure" ]]; then
    printf 'run-scene: failed: %s\n' "$failure" >&2
    if [[ -n "$dsl_errors" ]]; then
        printf 'run-scene: DSL error(s):\n%s\n' "$dsl_errors" >&2
    fi
    printf 'run-scene: last log lines:\n' >&2
    tail -20 "$run_dir/game.log" 2>/dev/null | sed 's/^/  /' >&2 || true
    exit 1
fi

exit 0
