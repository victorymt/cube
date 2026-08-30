# Stage 1: Solar Geometry and Radiative Balance

## Scope

`src/world/radiation_balance.[ch]` provides the first conservation-based
environment forcing layer. It is intentionally independent of chunk storage,
weather particles, and rendering so that its numerical contract can be tested
without a window or a generated world.

The module models an elliptical orbit, solar declination from axial tilt,
local solar elevation, daily-mean top-of-atmosphere insolation, and a grey
long-wave atmosphere. `RadiationColumnStateStep` turns the resulting net flux
into a column energy update using a configured heat capacity.

## Equations and Units

- Kepler's equation is solved for eccentric anomaly `E`: `E - e sin(E) = M`.
- Orbital distance is `r = a (1 - e cos(E))`, in astronomical units.
- Declination is `asin(sin(tilt) sin(trueLongitude))`.
- Solar zenith cosine is `sin(phi) sin(delta) + cos(phi) cos(delta) cos(H)`.
- Daily mean insolation uses the analytic sunset hour angle and is clamped to
  polar night/day. Flux values are watts per square metre.
- Grey greenhouse attenuation uses `epsilon_eff = epsilon / (1 + 0.75 tau)`.
- Absorbed short-wave flux is `S (1 - albedo)`. Outgoing long-wave flux is
  `epsilon_eff sigma T^4`.
- Column energy is joules per square metre and is advanced exactly by
  `energy += netFlux * deltaSeconds`; temperature is derived from
  `energy / heatCapacity`.

All public functions reject non-finite values. A column step is rejected when
the proposed energy would be non-positive, preserving the physical invariant
instead of silently creating a negative absolute temperature.

## Integration Boundary

The world module is included by the normal `WORLD_SRC` wildcard and the
focused `test_radiation_balance` target is registered in `mk/tests.mk`. No
existing weather or planet-climate output is changed in this stage. Later
stages can feed the output into local climate cells and use the column state as
the persistent atmospheric/surface energy store without changing the current
save format.

The existing game time and catalog climate solvers remain the compatibility
path for now. Their conversion to this time-dependent forcing belongs after
the unified simulation clock (Stage 0) and before water/biome feedback stages.

## Validation

`tests/test_radiation_balance.c` checks Kepler periapsis/apoapsis, equinox and
polar geometry, night-side insolation, greenhouse attenuation, invalid-input
clearing, and exact column energy accounting. The module has no global mutable
state, so repeated evaluations with identical inputs are deterministic.
