# Stage 2: Hydrology Model

## Scope

`src/world/hydrology_model.[ch]` is a deterministic, bounded vertical slice for
surface runoff, shallow groundwater, and sediment transport. It operates on a
small rectangular height-field grid and has no dependency on rendering,
chunks, or the block-fluid runtime. The world module wildcard therefore builds
it with the normal world archive, while callers can adopt it incrementally.

Each cell stores:

- bed elevation and erodible soil depth (metres);
- surface-water and groundwater depth (metres);
- suspended sediment load (kg/m2);
- the last-step runoff rate for diagnostics (m3/s).

The grid cell area is `cellSizeM * cellSizeM`. `HydrologyAddPrecipitation`
records precipitation immediately, so the cumulative water balance can be
audited without reconstructing weather samples.

## Step order

For a positive finite `dtSeconds` (clamped to one day), `HydrologyStep` runs
the following fixed order:

1. Remove evaporation from surface water.
2. Release a configurable fraction of groundwater to the surface (baseflow),
   then infiltrate surface water up to the unsaturated aquifer capacity.
3. Erode soil from cells with a positive downhill gradient. Eroded depth is
   converted to sediment using the configured bulk soil density.
4. Deposit the fraction of sediment above the local carrying capacity.
5. Compute all four-neighbour fluxes from the pre-routing state. Flux is
   proportional to downhill hydraulic-head difference and a bounded
   `1 - exp(-rate * dt)` flow fraction.
6. Apply the fluxes simultaneously. Water and sediment are transported with
   the source concentration, preventing update-order dependence.

The simultaneous flux pass is important for deterministic replay: incoming
water cannot be routed again until the next fixed step. Closed boundaries are
the default. With `openBoundaries`, edge flux is counted as outlet runoff in
`HydrologyStats.runoffOutVolumeM3`.

## Invariants and verification

The focused test target is `make -s build/normal/tests/test_hydrology_model`.
It verifies:

- invalid configuration and coordinate rejection;
- precipitation plus infiltration preserving total water volume;
- deterministic downhill routing over multiple steps;
- erosion lowering a bed, sediment reaching the downstream cell, and total
  soil-plus-suspended sediment conservation in a closed domain;
- excess sediment deposition increasing soil depth.

For a closed, non-evaporating grid, water mass is conserved exactly up to
floating-point rounding. Sediment mass is conserved between soil and suspended
load; open-boundary exports are reported separately. Evaporation and outlet
runoff are intentional sinks and are accumulated in the stats structure.

## Integration boundary and limitations

This stage does not yet sample `WeatherFieldSample` precipitation, write
terrain blocks, or replace `fluid.c`'s voxel solver. A future world-runtime
adapter should aggregate weather precipitation into grid cells, persist the
coarse hydrology state, and project river discharge back onto chunk water
levels. The current model has no channel width, saturated lateral aquifer
flow, snowpack, tidal boundary, or sediment grain classes; those are deferred
until a runtime adapter and profiling budget exist.
