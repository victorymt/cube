# Polar E2E reply ordering and diagnostics

Current coverage limit (2026-10-08): the recorded identity/protocol checks do not
establish polar ground coverage. The [landform replay](../landform-quality.md)
reproduces a narrow polar strip on wet and dry planets. The
[cube-grid foundation](../surface-cube.md) is tested but not integrated into
the runtime; polar visual acceptance remains open.

## Reproduction

The 2026-08-30 full graphics route timed out near its final south-pole input
scenario. On 2026-09-19, a fresh 1920x1080 DSL run completed the north/south
teleports and all 600 stationary input frames in about 14 seconds. The original
full game route also passed before the reply-ordering changes, in about 71
seconds. Both used the active Hyprland session and hardware rendering.

The historical performance timeout was not reproduced. Its original runtime
cause remains unconfirmed; this change does not claim an engine performance
fix. The final input command is a stationary 600-frame soak, not 600 frames of
movement across the pole.

## Confirmed harness defect

Every complete stdin DSL block terminates with a `DEBUG_SCRIPT complete`,
`error`, or `exit` reply. A command callback can emit `DEBUG_CONTROL` first.
Both graphical shell harnesses previously sent the next command after receiving
only that callback reply. The unread completion of the previous block could
then satisfy a later `wait` or `assert` check, shifting failure attribution and
allowing checks to return before their own statements completed.

The shared `tests/debug_stdin_protocol.sh` now consumes a pending block's
terminal reply before sending another complete block. All waits track terminal
replies, including expected errors and completions preceding deferred screenshot
captures. Multiline `repeat` statements are sent as one block. Timeouts name the
pending DSL command. The headless fake-peer regression explicitly rejects a
stale completion being mistaken for a successful `assert false`.

## Polar gate and artifacts

`make test-polar-e2e` runs `tests/scenes/polar_replay.dsl` in an isolated runtime
directory. It checks both pole rebases, canonical climate/ecology region identity,
absence of duplicate chunks, and the original 600 input frames. Progress is
reported after each 100-frame segment. The default wall budget is 90 seconds;
`POLAR_E2E_TIMEOUT` can explicitly override it. The full route's 300-second
command budget and 600-frame assertion are unchanged.

The optional Hyprland CI job runs the short graphics-control route and this
polar gate sequentially. `make test-e2e` retains the long combined-world route
and the graphics-control route. The stdin protocol regression is included in
`make test` through `make test-debug-stdin-protocol`.

All graphical routes use the existing DSL and Wayland session; no `hyprctl`
probe or input automation is required. Failures preserve temporary runtime
directories, traces, logs and captures, and print their paths. Set
`E2E_KEEP_ARTIFACTS=1` for the long/graphics routes or
`POLAR_E2E_KEEP_ARTIFACTS=1` for the polar route to retain successful runs too.

## Verification

Validated on 2026-09-19:

- `make -s -j4 all`: normal build passed.
- `make -s test-debug-stdin-protocol`: all fake-peer reply-ordering cases passed.
- `bash -n` passed for the shared helper, protocol test and all three graphical
  harnesses; `make -s test-architecture` and `git diff --check` passed.
- `POLAR_E2E_KEEP_ARTIFACTS=1 make -s test-polar-e2e`: passed, 600 input frames
  in 15 seconds against the 90-second budget, with a 1920x1080 capture.
- `make -s test-e2e` with artifact retention/export enabled: passed both the
  long game route (five validated captures, including
  the south pole) and pause/map/surface/view/look/mouse/ship controls. The long
  game's final trace sample was at frame 2094 and 64.35 seconds; the combined
  harness run, including batch-error contracts and the short route, took about
  78 seconds. Expected batch assertion/syntax failures retained their specified
  exit codes.
- All 29 local links in the updated documentation passed validation, and the
  codebase graph was refreshed after the shell changes.

The artifact-retention option preserved the full route's trace and reply log,
including outgoing commands. No production simulation or renderer changes were
needed, and neither the original input duration nor command timeout was relaxed.
