# Stage 02: WMO cloud taxonomy and layered cloud fields

## Purpose

Replace the single generic volumetric cloud slab with a meteorologically driven
cloud taxonomy. A weather sample may contain simultaneous high, middle, low,
and vertically developed cloud, and each visible layer must have a recognizable
height, thickness, coverage, motion, density profile, and precipitation role.

## Classification

Use the ten World Meteorological Organization cloud genera:

| Genus | Level/form | Primary model signal |
| --- | --- | --- |
| Cirrus | High, fibrous | Thin high moisture, wind and ice-crystal conditions |
| Cirrocumulus | High, cellular | High-level moisture with weak instability |
| Cirrostratus | High, sheet | Broad frontal lift ahead of precipitation |
| Altocumulus | Middle, cellular | Mid-level moisture and moderate instability |
| Altostratus | Middle, sheet | Deep frontal moisture and approaching precipitation |
| Nimbostratus | Low-middle, deep sheet | Sustained precipitation without deep convection |
| Stratocumulus | Low, lumpy sheet | Stable moist boundary layer with broken cover |
| Stratus | Low, uniform sheet | Near-saturated stable air, fog/drizzle relationship |
| Cumulus | Low, detached convection | Surface-driven instability without severe storm |
| Cumulonimbus | Deep vertical tower/anvil | Strong instability, storm, hail and lightning |

The model stores a bounded coverage value for every genus, a bitset of present
genera, and a dominant genus. Coverage must vary continuously across space and
time and remain deterministic for a fixed weather input.

## Layer construction

- Select at most one high, one middle, and one low/vertical representative for
  rendering; this caps ray-marched layers at three.
- Map physical cloud bases and depths into the compressed local render scale
  while preserving ordering: low below middle below high. Cumulonimbus spans
  low to high levels and suppresses redundant layers when opaque.
- Use per-genus render profiles for horizontal scale/stretch, billow/detail,
  vertical envelope, opacity, drift offset, and storm darkening.
- Thin high cloud uses fewer ray steps and low opacity. Deep precipitating cloud
  gets denser bases; cumulonimbus gets strong vertical development and an anvil
  bias. Render quality settings remain authoritative for the total budget.
- Keep the legacy aggregate cloud fields as derived compatibility values for
  sky darkening, fog, precipitation, and callers outside the cloud renderer.

## Debug and inspection

- Add `weather cloud GENUS COVERAGE FRAMES` to force a deterministic cloud
  profile without changing saved weather state.
- Add `weather cloud-clear` or equivalent behavior to return to natural cloud
  classification without clearing a separately forced weather phenomenon.
- `weather inspect`, DSL values, and screenshot reports expose the dominant
  cloud genus, present genera, layer count, layer bases, depths, and coverage.
- Accept genus names case-insensitively with hyphen/underscore normalization.

## Tests

- Classification is deterministic, bounded, continuous, and produces each of
  the ten genera under suitable controlled inputs.
- Atmosphere-disabled samples contain no cloud genera or layers.
- Precipitation relationships hold: nimbostratus/cumulonimbus dominate heavy
  precipitation; cumulonimbus dominates severe convection.
- Layer selection preserves altitude order, caps the layer count, and gives
  distinct profiles to sheet, cellular, fibrous, and vertically developed cloud.
- Debug parsing validates genus, coverage, duration, clear behavior, and errors.
- Existing weather, visual, screenshot, E2E, sanitizer, and performance gates
  continue to pass.

## Acceptance checklist

- [x] Ten cloud genera represented in the weather model.
- [x] Natural samples can contain concurrent high/middle/low cloud.
- [x] Renderer draws bounded, visibly distinct cloud layers.
- [x] Debug forcing and inspection cover every genus.
- [x] Unit, sanitizer, E2E, visual, and 1920x1080 Medium performance checks pass.
- [x] Stage commit prepared; this document is included in that commit.

## Recorded visual and performance evidence

- Forced genus captures at 1920x1080 Medium quality:
  `screenshots/voxelcraft_20260819_073646_000.*` through
  `screenshots/voxelcraft_20260819_073654_000.*`. The sequence covers Cirrus,
  Cirrocumulus, Cirrostratus, Altocumulus, Altostratus, Nimbostratus,
  Stratocumulus, Stratus, Cumulus, and Cumulonimbus; all ten PNG files passed
  `tests/validate_png.py` and their version 8 reports identify the expected
  genus and 1920x1080 Medium render settings.
- Natural layered capture:
  `screenshots/voxelcraft_20260819_073952_000.*`. The report records three
  altitude-ordered layers: Stratocumulus at 25.58, Altostratus at 50.80, and
  Cirrus at 88.88 render-height units.
- Performance report: `/tmp/stage02-cloud-perf.keyvalue`. Standard `--perf` at
  1920x1080 Medium reported CPU frame mean/p95 3.983/5.906 ms and GPU frame
  mean/p95 0.868/1.113 ms across 600 samples, with 1800 cloud draws,
  `sync_rebuilds=0`, empty pending mesh snapshots, and stable final/peak/repeat
  resource counts.
- Performance mode disables persistent weather block edits while retaining
  weather simulation and cloud rendering. Without that isolation, the fixed
  benchmark route could continually invalidate meshes and never reach its
  post-route stability checkpoint.

## Final verification

`make test`, `make test-sanitize`, `make test-long-run`, `make test-e2e`, and
`git diff --check` pass. Both normal and sanitizer suites report 70 passed and
0 failed; public-header, module-archive, clean-build guard, and architecture
checks also pass.

## Out of scope

Tornadoes, persistent wildfire, aircraft-scale cloud microphysics, global
forecast grids, and cloud-ground volumetric shadows are separate stages. Stage
02 may expose the convective/cloud-base inputs those systems will consume.
