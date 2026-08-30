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
