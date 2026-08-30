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
walking, swimming, or driving workload. Future replay integration should hash
clock, ecology, fluid, and hazard snapshots at each fixed tick.

Tests cover identical replay hashes, changed-input divergence, monotonic tick
validation, bounded capacity, and ordered playback.

## Verification evidence

On 2026-08-30, `make -s test` completed with 86 passing tests, including the
replay and all physics stages; clean-build and architecture checks passed. The
three local `make -s benchmark-chunks` samples reported medians of 36.862 ms,
38.036 ms, and 45.540 ms, with `mesh_snapshot_bytes=8192` and
`sync_rebuilds=0` on every run. The complete sanitized manifest also passed
with 86 tests and 0 failures under ASan/UBSan.

The CI graphics job runs the bounded `make test-debug-graphics-e2e` route,
which completed successfully in the
active Hyprland session, including pause/map/camera/look/mouse/ship routes.
The full `make -s test-e2e` reached and passed its weather, fire, tornado,
fluid, save/load, topology, and screenshot scenarios, but timed out during the
final long polar-input scenario under the current loaded session. It exited 2;
this remains an open performance/reliability issue for the full route and is
why the CI graphics job is opt-in on a dedicated Hyprland runner.
