# Stage 01: local climate and weather foundation

## Purpose

Replace the former small set of cosmetic weather states with a deterministic,
physically related local atmosphere model that works on Homeworld and suitable
solid planets. Establish the simulation, persistence, visual, damage, and debug
interfaces needed by later cloud, tornado, wildfire, ecology, and planetary
topology stages.

## Planned scope

- Derive local air temperature, pressure, relative humidity, dew point,
  wet-bulb temperature, instability, wind/gust, visibility, and precipitation
  phase from planetary climate and local conditions.
- Classify climates into named regimes and keep atmosphere/water-cycle
  eligibility explicit for unsuitable worlds.
- Provide varied precipitation, storm, wind, obscuration, temperature, and
  optical phenomena rather than a single rain/snow switch.
- Render clouds, fog/dust, precipitation, lightning, rainbow, and aurora with
  intensity-driven presentation.
- Apply bounded environmental effects: wetness, mud, snow, ice, erosion,
  wind/hail damage, lightning ignition, fire, and entity hazards. The player
  remains immune in this stage.
- Persist long-lived affected surfaces and fires while keeping forced debug
  weather runtime-only.
- Add a user setting for environmental weather effects.
- Expose deterministic debug commands, DSL values, screenshot diagnostics,
  and fixed-resolution/performance validation support.

## Implemented design

### Climate and weather

- `LocalClimateState` classifies 14 regimes: vacuum, hot greenhouse, ice cap,
  tundra, boreal, desert, steppe, tropical rainforest, monsoon, savanna, humid
  continental, oceanic, Mediterranean, and temperate.
- `WeatherFieldSample` carries temperature, pressure, humidity, dew point,
  wet bulb, instability, wind/gust, visibility, cloud base and cover, and
  separated precipitation phases.
- The deterministic phenomenon set contains 20 entries: clear, cloudy, fog,
  frost, drizzle, showers, heavy rain, thunderstorm, lightning, sleet,
  freezing rain, hail, snow, blizzard, strong wind, dust storm, heat wave,
  cold snap, rainbow, and aurora.
- Forced phenomena bypass natural rarity and eligibility only for debug runs;
  normal simulation still follows atmospheric and water-cycle constraints.

### Environmental effects

- Weather-owned surface records track wetness and persistent snow/ice/mud
  transformations without scanning the entire world.
- Block catalog entries expose wind resistance, impact resistance,
  flammability, and water erodibility.
- Rain, freeze/thaw, hail, high wind, lightning, and fire operate on a bounded
  fixed-rate update budget. Disabling weather damage restores reversible
  weather-owned changes and clears active weather fires.
- Non-player entities can receive temperature, wind, hail, and lightning
  hazards. Player damage is intentionally excluded pending a dedicated survival
  balance stage.

### Persistence and controls

- World save format V20 stores weather-impact state and remains compatible with
  earlier supported saves.
- Settings include an environmental weather-effects toggle.
- Debug control supports `weather inspect`, `weather force`, `weather clear`,
  `weather damage`, and `weather step`.
- DSL values expose climate, atmospheric values, precipitation phases, optical
  effects, affected surfaces, fires, forced frames, and damage state.
- Screenshot format 7 records weather diagnostics; performance DSL values and
  `--debug-resolution WIDTHxHEIGHT` support deterministic 1080p validation.

## Acceptance checklist

- [x] Climate and phenomenon rules have deterministic unit tests.
- [x] Weather-impact mutation, cleanup, save/load, and material response have
  focused unit tests.
- [x] Entity hazards and player immunity are covered.
- [x] Settings and V20 backward compatibility are covered.
- [x] Debug commands, DSL values, diagnostics, and option parsing are covered.
- [x] All 20 phenomena have been captured and visually inspected at 1920x1080
  Medium quality.
- [x] Performance report passes the 60 FPS frame budget with no synchronous
  rebuilds and stable resource counts.
- [x] Current worktree passes the final repository verification immediately
  before the stage commit.
- [x] Stage commit prepared; this document is included in that commit.

## Recorded visual and performance evidence

- Visual captures: `screenshots/voxelcraft_20260819_011320_000.*` through
  `screenshots/voxelcraft_20260819_011348_000.*`.
- Performance report: `/tmp/weather-perf.keyvalue`.
- Reported Medium 1920x1080 CPU frame mean/p95: 4.145/6.148 ms.
- Reported Medium 1920x1080 GPU frame mean/p95: 0.476/0.724 ms.

These artifacts prove the foundation set only. They do not cover the next-stage
cloud taxonomy, tornado, or extended wildfire behavior.

## Deferred to Stage 02

- Low, middle, high, and vertically developed cloud families with distinct
  bases, thickness, precipitation relationships, and recognizable silhouettes.
- Tornado genesis from severe convective storms, lifecycle and track, debris,
  localized block/entity forces, visibility, audio/visual presentation, debug
  forcing, and performance limits.
- Wildfire spread driven by fuel, dryness, temperature, wind, slope/exposure,
  rain suppression, firebreaks, persistent burn state, smoke, and regeneration
  hooks. Stage 01 lightning fires are only the ignition foundation.

## Verification commands

```sh
make test
make test-sanitize
make test-long-run
make test-e2e
git diff --check
```

Final result on 2026-08-19: all commands passed. Both normal and sanitizer
suites reported 70 passed and 0 failed; public-header, module-archive,
clean-build guard, and architecture checks also passed.
