# Cross-system replay verification

## Scope

The P2 verification stage combines the existing simulation clock, recorded
input events, regional ecology, voxel fluids and weather impacts in one bounded
headless fixture. It uses production simulation and persistence APIs while
replacing graphics and terrain streaming with a deterministic loaded scene.

`tests/test_simulation_integration.c` records an 80-tick input route with
`SimulationReplay`, then consumes it through `SimulationClock` callbacks. Each
0.5-second callback advances the actual space clock, weather sampling, regional
ecology, a weather-impact tick and five fluid ticks. The loaded scene contains a
confined water column and burning wood. Scripted dry/rain forcing gives the
weather-impact model reproducible conditions; ecology still samples real weather.

Each tick records separate clock, weather, ecology, block/water and hazard hashes.
Comparisons report the first differing tick and subsystem. Non-empty lineages,
changing populations, transferred water, an active fire at the checkpoint and
rain deposition prevent empty or inert fixtures from satisfying the gate.

The ecology trace includes public population/evolution samples plus the complete
12-field nutrient state, initialization flag and last update time. A test-only
`PlanetEcologyTestPopulationSnapshotAt` copies an existing canonical region under
the population mutex without creating records, advancing time or changing cache
access order. This detects stored nutrient differences even before they affect
visible population density.

The route checks:

- Equal inputs under 0.5-second frames and 0.125/0.125/0.25-second partitions.
- Uninterrupted versus restored continuation after tick 16, with settled water
  and active ecology/fire state.
- An equivalent route queried through a full longitude-wrap alias.
- Movement to a neighboring ecology region, a harvest disturbance and return.
- Changed fire intensity, harvest count and water volume: their respective
  subsystem must first diverge at ticks 4, 28 and 48, with all earlier states equal.
- Two independent restores of the same active-flow checkpoint, followed by
  identical inputs under different frame partitions.

Snapshots hash explicit scalar fields in canonical order, with normalized signed
zero and finite floating-point checks. They must not hash C struct padding,
pointers, cache placement or wall-clock performance counters. Exact hashes are
comparisons within the same build, not a cross-platform numerical guarantee.

## Persistence boundary

The checkpoint serializes production space, planet, ecology, fluid and
weather-impact payloads. The fixture retains its small block/edit snapshot and
input cursor separately; the clock is rebuilt from the loaded space time. This
tests the subsystem persistence composition, not the full `GameSaveMap` file
transaction or the streaming loader.

The existing FLD2 payload persists water edits and procedural baselines. Loading
reconstructs active fluid work and resets its queue, counters and fractional
update budget. It does not promise identical intermediate flow after an arbitrary
mid-flow save. Uninterrupted-versus-restored comparisons therefore need a settled
fluid checkpoint at a complete simulation tick. Weather-impact update fractions
are likewise not persisted, so checkpoints must align with its update cadence.

The scene has one loaded chunk and a bounded set of ecology queries. The regional
route validates canonical coordinates and disturbances across ecology regions;
it does not unload chunks, rebase the world origin, evict the ecology cache or
verify off-screen catchment simulation. The weather-impact fixture fits within
one work-budget pass so unsaved processing cursors cannot affect checkpoint
continuation.

The integration gate does not migrate frame-driven gameplay systems into the
production fixed-step callback, simulate GPU rendering, or replace the graphical
Hyprland E2E route.

## Commands

`make test-simulation-replay` runs the input-log unit tests and the coupled replay
route. Both also run through the normal and sanitizer test manifests. The route
uses `FluidStepTicks` rather than `FluidUpdate` so CPU wall-clock budgets cannot
change the number of simulated cells between runs.

## Verification evidence

Validated on 2026-09-19:

- The focused integration binary passed in approximately 0.8 seconds; all
  frame-partition, checkpoint, longitude-alias, regional-coordinate, nutrient
  and changed-input assertions passed.
- `make -s -j4 test`: 92 passed, 0 failed, including 149 public headers,
  seven component archives, plugin/debug CLI checks and the architecture gate.
- `make -s -j4 TEST_TIMEOUT_SECONDS=300 test-sanitize`: 92 passed, 0 failed
  with ASan/UBSan and leak detection enabled. The integration route took
  approximately 2 seconds. The existing entity/ecology test took 86 seconds
  normally and 178 seconds with instrumentation, hence the sanitizer run's
  explicit 300-second per-test budget.
- Updated documentation links and `git diff --check` passed. The codebase graph
  was refreshed after the source changes.

The full graphical E2E route was not rerun for this headless verification stage;
its subsequent successful recheck and the historical timeout investigation are
recorded separately in [record 15](15-polar-e2e-reliability.md).
