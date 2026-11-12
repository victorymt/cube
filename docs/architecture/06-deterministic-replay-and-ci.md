# Architecture: Deterministic Replay and Verification Gates

`SimulationReplay` records a bounded sequence of `(fixed-step tick, action,
value)` events. Ticks must be monotonic, the event capacity is explicit, and a
canonical little-endian FNV-1a hash is calculated from fields rather than C
struct padding. Playback is read-only and returns events in recorded order.
This is the input boundary for replaying the fixed-step simulation clock and
comparing state hashes from two runs.

The unit-test job continues to run the complete C test manifest. A dedicated
performance job runs `make benchmark-chunks` on every CI event. A graphics E2E
job is available on a self-hosted runner tagged `hyprland` and is opt-in via
the repository variable `VOXELCRAFT_HYPRLAND_RUNNER=true`; the existing DSL
scripts require a real Wayland socket and therefore are not falsely reported
as passing on headless GitHub-hosted runners.

The current benchmark route injects a fixed player position and measures chunk
generation/meshing, so it is a streaming regression gate rather than a full
walking, swimming, or driving workload. The
[cross-system replay gate](14-cross-system-replay.md) adds per-tick state
comparisons for clock, weather, ecology, voxel water and fire in a bounded
headless scene. It documents the save/restore boundary separately from arbitrary
mid-flow saves and from the production frame-driven gameplay loop.

Tests cover identical replay hashes, changed-input divergence, monotonic tick
validation, bounded capacity, and ordered playback.

## Focused local verification

The complete manifest remains the CI and pre-merge gate, but it is not the
default loop for a single-module change. Build and run one registered test with:

```sh
make test-one TEST=test_player_collision
```

The normal escalation is:

1. **Local fix:** the directly affected unit test, plus its E2E when the change
   is player-visible.
2. **Cross-component change:** `make test` for public headers, build/test
   manifests, scheduling, persistence, surface contracts or shared formats.
3. **Merge/release:** `make test-clean-root`; use `make test-ci` only for the
   full CI-equivalent sanitizer/coverage/clean-root sequence.

Documentation-only changes run `make check-docs` and, when runtime field names
changed, `make docs-probe`. A missing direct test is a reason to add one, not a
reason to run the full suite as a substitute.

Run the focused input-log and cross-system tests with
`make test-simulation-replay`. Both are also included in `make test` and the
sanitized test matrix. Current cross-system results are recorded in
[record 14](14-cross-system-replay.md); the evidence below remains dated to its
original run.

## Verification evidence

On 2026-08-30, `make -s test` completed with 86 passing tests, including the
replay and all physics stages; clean-build and architecture checks passed. The
three local `make -s benchmark-chunks` samples reported medians of 36.862 ms,
38.036 ms, and 45.540 ms, with `mesh_snapshot_bytes=8192` and
`sync_rebuilds=0` on every run. The complete sanitized manifest also passed
with 86 tests and 0 failures under ASan/UBSan. <!-- volatile-counts:allow -->

The CI graphics job runs the bounded `make test-debug-graphics-e2e` route,
which completed successfully in the
active Hyprland session, including pause/map/camera/look/mouse/ship routes.
The full `make -s test-e2e` reached and passed its weather, fire, tornado,
fluid, save/load, topology, and screenshot scenarios, but timed out during the
final long polar-input scenario under that loaded session. That run exited 2.

On 2026-09-19 the original full route passed again, so the historical timeout's
runtime cause could not be reproduced. A confirmed stdin reply-ordering flaw was
corrected in both graphical harnesses, followed by successful full and short
graphics routes and a new independent 600-frame polar gate. See
[record 15](15-polar-e2e-reliability.md) for current evidence and limits. The CI
graphics job remains opt-in on a dedicated Hyprland runner and now also runs the
bounded polar gate.
