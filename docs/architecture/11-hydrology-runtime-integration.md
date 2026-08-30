# Hydrology Runtime Integration

## Boundary

The hydrology runtime in `src/world/hydrology_runtime.[ch]` is a bounded,
process-level bridge for the currently active surface. It deliberately sits
above voxel `FluidUpdate`: voxel fluids preserve local block gameplay, while
this solver tracks catchment-scale water, soil, groundwater, runoff and
sediment for climate feedback.

## Spatial and temporal budget

One canonical 128x128 map region is active at a time. The region is sampled on
a 32x32 grid with 4 metre cells. Binding uses `surfaceId`, `regionX` and
`regionZ`, so longitude wrapping and pole aliases retain one identity. The
runtime uses a fixed step and an accumulator with a bounded catch-up count;
render frames never mutate solver state directly.

## Inputs and feedback

Each coarse cell receives terrain elevation, soil depth and weather
precipitation. The solver advances surface water, groundwater, soil storage,
runoff and sediment. `WeatherFieldSampleAtWorldTime` supplies normalized rain
intensity, converted to metres per second. The resulting local feedback is
provided to `LocalClimateEvaluate`, affecting humidity, wetness, drought and
regime classification while preserving legacy behaviour when no hydrology
sample is active.

## Persistence

`WORLD_SAVE_FORMAT_V22` appends a magic/version-checked hydrology payload after
the radiation payload and before map markers. Older V18-V21 saves reset the
hydrology bridge. A malformed V22 payload fails transactionally and the outer
save loader still rejects trailing bytes.

## Diagnostics and limits

The Debug DSL exposes active state, canonical region, step count, water and
sediment totals, and the player's cell feedback. Current limits are intentional:
there is no global river network, ocean transport, snowpack, lateral
groundwater solver, or off-screen catchment integration. Those belong to a
future regional/global hydrology stage; the bounded bridge is the realistic
runtime seam needed before connecting nutrient ecology and fire suppression.

## Evidence

- `tests/test_hydrology_runtime.c` covers binding, fixed-step/catch-up,
  feedback, round-trip state and corruption rejection.
- `tests/test_game_save_spherical.c` covers V22 payload routing, legacy reset,
  truncation and trailing-byte handling.
- DSL and graphical E2E checks wait for `hydrology.active` and assert canonical
  region continuity across a teleport and a save/load cycle.
