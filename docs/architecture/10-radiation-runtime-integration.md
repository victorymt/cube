# Radiation Runtime Integration

## Boundary

The radiation runtime is an instance-based, fixed-step column model in
`src/world/radiation_runtime.[ch]`. The game owns one process-level bridge for
the active surface. `GameAdvanceSimulationStep` advances space time first, then
binds the bridge to the current surface body and canonical map region.

Surface weather reads the bridge through a per-cell temperature offset. A cell
outside the active 128x128 canonical region receives no offset, so sampling a
remote location cannot inherit the player's local column state.

## Time and Sampling

The existing `SimulationClock` remains the authoritative fixed-step scheduler.
The radiation bridge consumes the same step duration and uses the callback's
post-step simulation time, wrapped with `SimulationTimePeriodic` for orbital
forcing. Rendering and weather queries may sample between fixed steps, but the
column itself only changes at fixed-step boundaries.

## Region Budget

Only one 128x128 canonical surface region is integrated at a time. Binding is
identified by `surfaceId`, `regionX`, and `regionZ`; a body or region change
reinitializes the column at the local climate baseline. This keeps the runtime
constant-cost while preserving spherical longitude wrapping and pole aliases.

## Profiles

Planet surfaces use the persisted `PlanetWorldProfile`. Homeworld uses the
legacy temperate profile generated from the current world seed, allowing the
same radiation balance equations to serve both surface kinds without changing
the homeworld terrain model.

## Save Migration

`WORLD_SAVE_FORMAT_V21` introduced a versioned, magic-checked radiation
payload after the weather-impact state. V22 retained that payload and added the
[hydrology state](11-hydrology-runtime-integration.md). V23 added a homeworld daylight anchor. The current writer retains these
payloads; see the [persistence index](README.md#implementation-verification-and-limits)
for the current format and later additions. V29 is now the only accepted world
format. Historical subsystem tests retain the V18-V20 reset and V21 corruption
cases; those fixtures do not imply whole-file compatibility. Invalid retained
radiation payloads still fail transactionally, and trailing bytes are rejected.

## Known Limits

The column is a local climate approximation, not a full global circulation
model. Heat transport between regions, ocean currents, cloud microphysics, and
vertical atmospheric layers are not yet simulated. The 128x128 binding is an
intentional performance budget and the next climate stage should add regional
water and ecology feedback without turning every loaded chunk into a solver.

## Evidence

- Radiation runtime unit tests cover fixed-step integration, catch-up limits,
  forcing construction, canonical region identity, offsets, and state I/O.
- Existing weather and save-format tests continue to exercise derived dew-point
  and wet-bulb values and legacy format compatibility.
- Full project test, sanitizer, and architecture checks are required before
  the stage commit.
