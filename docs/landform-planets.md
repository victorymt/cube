# Surface-neutral landforms

## Build contract and units

`LandformBuildWithInput` takes an explicit sea level, baseline callback and
borrowed callback context, strategy ID and `LandformPhysics`. The callback
returns elevation in surface blocks, normalized long-term effective runoff
(0–1), and hardness (0–1). It must not query live weather, hydrology or the
landform runtime. Construction is synchronous; the atlas retains no callback
or context pointer. Readers only access the finished immutable atlas.

`LandformBuild` supplies the historical defaults through this contract. Its
priority flood, tie-breaking, discharge proxy, river widths, lake blending,
deposits and version-controlled terraces are unchanged. Physical metadata
lives on the atlas, not in `LandformSample`. At this stage physical radius does
not rescale the historical gameplay geometry.

Physics uses meters, seconds, square meters, cubic meters/second and signed
radians/second. Normalized model discharge is not an SI volume flow rate:
callers must supply a physical effective precipitation rate to
`LandformDischarge`. Positive finite radius, block scale and gravity are
required; rotation may be zero or negative. Invalid physical inputs or no
liquid water reject an ordinary drainage build with NULL. Callers distinguish
unsupported capability from allocation failure before requesting a build.

The Coriolis diagnostic uses f = 2 omega sin(latitude) and the unconstrained
reach estimate d = f L² / (2 v). Stagnant flows below 1e-6 m/s yield zero
deflection. This is a diagnostic estimate, not a river migration simulation.
A displacement of at least one block sets `resolvableDeflection`; it never
changes geometry in terrain versions 7 or 8.

Fingerprints serialize named numerical fields into fixed byte order, ignoring
struct padding and canonicalizing negative zero and nonfinite values. Input
validation remains separate; equal invalid fingerprints do not authorize a
build.

## Compatibility evidence

`test_landform` compares full drainage cells and every sample field between the
legacy wrapper and the explicit contract, with terraces both off and on.
`test_terrain_scale` locks Homeworld v7/v8 drainage and sample fingerprints to
those captured from the pre-contract model (commit 4308ea8), including a known
v8 terrace. Its existing tests also cover voxel water, sediment, atlas eviction
and reconstruction. Sample equality deliberately excludes unspecified C
padding bytes.

## Surface runtime and generated blocks

`LandformRuntimePrepareSurface` receives the complete surface identity and its
selected strategy. Its four-slot cache includes kind, body, seed, terrain
version, profile, physics and the full strategy fingerprint (including sea
level and capabilities). Preparation/reset require drained chunk workers;
queries never build an atlas or consume RNG. Unsupported surfaces return no
atlas. Snapshot/profile and generation-version checks reject stale selections.

`PlanetTerrainSampleAt` combines the planet's persistent baseline with its
optional atlas. Planet height queries, chunk columns and debug inspection use
this path. River/lake water is filled to the sampled level, deposits seal beds
before cave carving, and decorations do not replace river/lake water. Without
an atlas the previous planetary height, biome and liquid rules remain active.
Homeworld v7/v8 continue to use the original sampling and material path.

Coriolis remains diagnostic-only in every currently supported terrain version;
`landform.coriolis_enabled` is false even when `resolvable_deflection` is true.
A future version must explicitly opt into a separately validated geometry rule;
the one-block threshold alone never changes existing terrain. SI discharge in
runtime diagnostics is an estimate using normalized runoff times 1e-5 m/s,
not a calibrated climate transport simulation.

`test_terrain_scale` checks actual planetary river/lake water and dry
sediment blocks, cache eviction/reconstruction, parameter and body isolation,
longitude aliases, and old-version/unsupported fallbacks.
`bash tests/test_landform_planets_e2e.sh` runs the DSL replay under Hyprland in
an isolated temporary save directory and checks save/load and body switching.
