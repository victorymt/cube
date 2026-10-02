# Atmospheric column model

The atmosphere has five physical layers and a vacuum state. Every sample
returns height (metres), kinetic temperature (K), pressure (atm), mass density
(kg/m³), density relative to the reference surface, remaining pressure
fraction, mean molar mass (kg/mol), and bounded ozone/ionization proxies.
There is no persistent atmosphere state; columns are derived from existing
planet profiles, so saved-world formats do not change.

## Reference column

Earth's reference surface is 288.15 K, 1 atm, 9.80665 m/s², and
0.0289644 kg/mol. The lower column follows standard-atmosphere temperature
segments, including the isothermal tropopause and stratopause intervals.

| Layer | Reference altitude | Temperature behaviour |
| --- | --- | --- |
| Troposphere | 0–11 km | 288.15 → 216.65 K, −6.5 K/km |
| Stratosphere | 11–51 km | Isothermal to 20 km, then rises to 270.65 K; plateau at 47–51 km |
| Mesosphere | 51–86 km | Falls through 214.65 K at 71 km to 184.65 K |
| Thermosphere | 86–500 km | Rises towards a stellar-heating-dependent exospheric temperature |
| Exosphere | 500–1000 km | Approximately isothermal; smooth transition to vacuum |

Pressure is integrated from each segment's base using
`d(log P)/dz = −Mg/(RT)`. The linear-temperature integral and its isothermal
limit share boundary pressures, ensuring pressure and temperature continuity.
Density comes from `ρ = PM/(RT)`, with pressure converted to pascals.
Mean molar mass gradually decreases above 86 km. Upper-segment gravity uses
a bounded reference-radius approximation; the lower reference column uses
standard geopotential-height gravity. The tail fades smoothly to zero between
500 and 1000 reference km so the finite simulation does not end in a density
jump. Invalid inputs return an invalid zero sample; a valid airless column
returns `Vacuum` without division by zero.

## Other planets

Physical layer heights scale with `T / (M g)`. Surface temperature, pressure
and gravity come from the existing planet profile. Breathable atmospheres
have an ozone inversion; non-breathable atmospheres do not receive one by
default. Gas giants use a hydrogen/helium mean mass, thin/corrosive atmospheres
use a carbon-dioxide mean mass, and icy dense atmospheres use nitrogen.
These are explicit composition approximations: current saved planet profiles
do not contain a chemical abundance inventory. Exospheric temperature scales
with stellar irradiance and is bounded to 200–3000 K.

The neutral solver is `world/atmosphere_model`; `space/planet_atmosphere`
converts existing planet data and game coordinates. Neither owns mutable
world state. Weather passes its planet snapshot and local surface temperature
into the same solver; spatial/seasonal surface differences are intentional.

## Game integration

Terrain retains one metre per voxel above reference Y=12. Above the highest
possible terrain plus 18 voxels, a continuous piecewise mapping fits the
remaining column into the existing launch corridor. It allocates 20%, 40%,
55%, and 85% of that corridor to the tropopause, stratopause, mesopause, and
exobase. The existing world-transition threshold is separate from optical
visibility: a dark sky must not launch a ship prematurely.

- Flight drag/exhaust use mass density, normalized to terrestrial air and
  bounded for the existing flight controller. The ship HUD reports local
  density as a percentage of that column's surface density.
- Sky fading, star extinction and planetary sky haze use remaining column
  pressure with a perceptual exposure response.
- Local fog, precipitation veils, particles and rain audio fade with air
  density and a smooth weather envelope around the tropopause. The regional
  weather field and the cloud geometry below the observer remain intact.
- The DSL's `atmosphere.*` namespace samples player altitude; `weather.*`
  continues to describe the regional surface weather.

The thermosphere/exosphere are a deterministic approximation, not a kinetic
plasma or atmospheric chemistry simulation. Their high temperature describes
individual particle energy and does not imply dense hot air or automatic
player heat damage. Remote planet silhouettes still use the existing
approximate scattering shell; this change supplies a stratified physical
column and local sky response, not a full volumetric multiple-scattering
renderer.

## Validation

`test_atmosphere_model` compares lower-column temperatures and pressures with
standard Earth reference values, checks all internal boundaries, monotonic
pressure/density, the vacuum tail, negative/non-finite heights, gravity and
composition changes, and both physical/game coordinate conversions.
Climate, weather and ship tests verify integration, including precipitation
stopping aloft and returning on descent. `tests/test_atmosphere_e2e.sh` runs
five-layer DSL assertions and captures the actual game on Hyprland.
