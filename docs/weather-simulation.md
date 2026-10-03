# Weather simulation clocks and bounds

## Temperature anomalies

Weather adds a pressure-front anomaly and rare warm/cold pulses to the local
climate temperature. Both pulses use mirrored upper-tail thresholds on the
same deterministic noise field. The previous cold thresholds applied a full
7 K cooling pulse throughout most of the field, shifting the weather baseline.

The regression holds the temperate climate input constant across 65,536
deterministic seed/position/time samples. It requires a near-zero mean anomaly,
substantial warm and cold populations, and rare excursions in both directions.
The mixed-precipitation fixture targets an actual sampled temperature near
freezing rather than depending on a particular noise anomaly.

## Impact time

`WeatherImpactUpdate` feeds a fixed 2 Hz impact clock. Every impact tick ages
all current-surface records, advances all loaded current-surface fires by
0.5 seconds, and advances current-surface burn recovery by 0.5 seconds. A
record's speed therefore does not depend on how many unrelated records exist.
Wetness evaporation and snow/ice melt keep their existing per-tick rates.
Melted weather-owned blocks are restored once loaded, preserving the original
water volume and the existing player-edit ownership checks.

Previously, round-robin limits processed only 64 surfaces, 16 fires and
32 burn sites per tick without accounting for time between visits. At capacity,
that reduced their simulation speeds by a factor of sixteen. The fixed-size
sets are small enough to advance directly, avoiding deferred-time approximation
when rain or temperature changes and avoiding a new persisted catch-up queue.

Work remains bounded by existing capacities:

| Work per impact tick | Upper bound |
| --- | --- |
| Existing surface records | 1,024 |
| Fires entering the fire pass | 256 |
| Burn recovery records | 512 |
| Newly sampled surface columns | 8 |

The passes traverse records backwards so swap-removal cannot skip an
unprocessed record or update one twice. A fire created by spread is appended
after the pass's initial bound and receives its first numerical step on the
following tick. Each existing fire still makes at most one spread attempt per
tick; the hard active-fire capacity prevents unbounded growth.

Records on other surfaces remain paused. Fire combustion also pauses while
its block is unloaded. Surface moisture/melt and burn recovery on the current
surface remain numerical state updates even when their blocks are unloaded;
terrain changes require loaded blocks. Disabled weather damage stops impact
ticks. This does not simulate unloaded fire combustion or historical catch-up.

## Local weather inputs

The game supplies `WeatherFieldSampleAtWorldTime` and the fixed-step world time
through `WeatherImpactWeather`. Surface evaporation/melt, new precipitation,
lightning ignition, each fire's combustion, spread-target fuel moisture, and
burn recovery sample their own X/Z column. Moving the player therefore no
longer substitutes their climate for records elsewhere on the same surface.
Discovery still follows the player and retains its eight-column budget.

A bounded 256-entry cache shares exact-column samples within each tick. It is
cleared for every new tick and explicit ignition, so time, surface and forced
weather changes cannot reuse a previous tick's climate. It does not quantize
biome boundaries. Natural weather sampling still includes the existing live
hydrology/radiation feedback where available. Forced/manual weather remains
spatially uniform through the normal weather sampler; cloud-only forcing keeps
natural local climate. The DSL's `weather step` samples at the current world
time, which remains frozen during the command.

An explicit uniform input without a sampler is retained for isolated unit
fixtures and coupled replay. Runtime and DSL callers use the spatial sampler.
Tests compare cold/dry and warm/rainy columns at the same time, player movement,
new rain deposition, ignition moisture and time-based cache invalidation.

## Persistence and validation

The `WXIMPACT2` payload is unchanged and `WXIMPACT1` migration remains supported.
There are no longer unsaved processing cursors. Fractional update time is still
runtime-only, so exact continuation is tested at complete impact ticks.

`test_weather_impact` compares the same isolated records with populations of
1/1/1, 65/17/33 and 1,024/256/512. It compares each tick across cold, warm, dry
and rainy inputs, across save/load, and across frame partitions. Separate cases
cover inactive surfaces, unloaded fires, disabled impacts, fully occupied
snow/ice restoration, preserved water volumes, and deferred spread births.

A local CPU probe using the test world's stub terrain callbacks measured 500
full-capacity single ticks: about 0.29 ms mean and 0.41 ms maximum, compared
with 0.025 ms mean for the old partial updates. This is a correctness/per-tick
cost tradeoff, not a speedup or a rendered-game frame-time measurement. Actual
chunk queries, block mutations and mesh rebuilds can cost more; the DSL and
debug trace remain the tools for measuring those in the live world.

## Cloud altitude

Cloud layers use the active surface's sea level as a fixed vertical datum.
Worlds without a sea level, including flat test worlds, use the atmosphere's
reference Y=12. `WeatherVisualStateAtWorld` supplies this datum in the same
snapshot as the cloud layers; the renderer no longer queries the four terrain
columns under the camera to reposition the cloud volume every frame.

Weather can still change layer bases and thicknesses. Moving the camera uphill
does not lift the cloud volume with it, and a mountain can intersect a low cloud
layer. Using one surface datum also avoids discontinuities at artificial region
boundaries and recomputes immediately on a world/planet transition.

`make test-cloud-altitude-e2e` runs the DSL route in
`tests/scenes/cloud_altitude_replay.dsl` on Wayland. It fixes weather and cloud
type, compares world-space cloud bases after horizontal/vertical movement and
surface transitions, and retains four screenshots plus the trace in a printed
temporary directory. Unit coverage also checks dry-world fallback and changing
terrain heights independently of the camera position.

Screenshot reports include `weather.cloud_reference_y` from the rendered
frame snapshot, alongside each layer's relative base height. The replay checks
these values as well as the live DSL fields. Planet screenshots use the debug
surface fixture without waiting for every terrain section to generate; this
route verifies clouds and surface transitions, not terrain-stream completion.
