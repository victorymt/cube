# Stage 04: Persistent wildfire, smoke, suppression, and recovery

## Purpose

Replace the existing minimal invisible fire records with a bounded wildfire
system. Fire must depend on fuel, fuel moisture, weather, terrain, and an
ignition source; spread coherently with wind and slope; remain visible and
hazardous while active; respond to precipitation and local water; persist
across saves and unloaded surfaces; and leave a decaying disturbance signal
that later vegetation stages can use for succession and recovery.

## Fire behavior

- Add a pure wildfire model for equilibrium fuel moisture, drying/wetting,
  combustion phase, fuel consumption, heat release, smoke production, and
  spread probability. Inputs are finite and outputs are bounded so randomized
  property tests can cover the full domain.
- Use `igniting`, `flaming`, and `smoldering` phases. Intensity and fuel
  moisture move continuously; rain or suppression reduces heat rather than
  deleting every fire immediately. Saturated fuel cannot ignite or spread.
- Fuel load and burn duration derive from block flammability and material
  family. Light vegetation ignites and burns quickly, logs and construction
  wood burn longer, and nonflammable blocks reject ignition.
- Natural ignition requires a source. Lightning can ignite sufficiently dry
  exposed fuel, and loaded flammable blocks adjacent to lava can ignite.
  Temperature and drought affect readiness but never create spontaneous fire.
- Spread candidates are selected deterministically from bounded neighboring
  cells. Probability responds to source intensity, target fuel and moisture,
  wind alignment, gusts, uphill slope, and vertical continuity. Downwind and
  uphill spread is favored without making crosswind or backing fire impossible.
- Keep at most 256 active fire records and process a bounded rotating subset on
  every fixed 2 Hz weather-impact tick. No radius scan or whole-world scan is
  permitted.

## Suppression, damage, and recovery

- Rain, wet fuel, water-filled neighboring cells, and explicit suppression
  cool fire and raise fuel moisture. Strong sustained suppression transitions
  flaming fire through smoldering before extinction.
- Add debug suppression over a bounded radius. It uses the same cooling and
  moisture path as natural water, reports affected fire count, and cannot
  mutate unloaded surfaces.
- Burning blocks are removed only after their fuel is consumed. Damage remains
  an environment mutation and is disabled by performance mode or the existing
  weather-damage setting.
- Fire heat and smoke damage nearby ecological entities with smooth distance
  falloff. Aquatic/immersed entities and sheltered positions receive reduced
  exposure. Player health is out of scope because the current player model has
  no general health contract; camera smoke and audio still communicate danger.
- Record a bounded set of burned sites with severity and recovery. Wet,
  temperate conditions recover faster than cold or arid conditions. Expose a
  local burn-severity query as the hook for Stage 06 succession and regrowth.
- Active fires and burn sites persist. Saving writes a new explicit disk
  schema; loading remains compatible with legacy `WXIMPACT1` fire records and
  validates counts, coordinates, phases, and finite ranges before committing
  state.

## Presentation and audio

- Render world-space flames at loaded active fire cells and deterministic smoke
  columns that lean with wind. Flame height, color, smoke density, rise, and
  opacity respond to phase, intensity, moisture, and fuel.
- Graphics quality controls the number of flame tongues and smoke puffs. Only a
  bounded nearest set is rendered; distant and unloaded fires remain simulated
  and persisted without presentation work.
- Add near-field smoke haze based on accumulated local plume exposure. It must
  not hide the UI, become opaque, or affect scenes outside the active surface.
- Add synthesized fire crackle/roar to the ambient mixer. Volume follows heat,
  distance, shelter, and immersion, and remains independent of rain, wind,
  thunder, and tornado channels.

## Debug and inspection

- Add `weather fire ignite X Y Z INTENSITY`, `weather fire suppress X Y Z
  RADIUS [AMOUNT]`, and `weather fire clear`. Commands require a loaded active
  surface. Clear removes active fire while preserving weather/cloud/tornado
  forcing and burn history.
- `weather inspect`, typed DSL values, and screenshot reports expose active fire
  count, nearest distance and phase, nearest intensity/fuel/moisture, local
  heat/smoke, cumulative ignitions/spread/extinctions/suppression, burned blocks,
  burn-site count, recovered sites, and dropped fire/burn records.
- Screenshot report schema advances from version 9 to version 10.

## Tests and acceptance

- [x] Model outputs are finite, bounded, and deterministic; saturated fuels
  reject ignition and downwind/uphill spread exceeds backing/downhill spread.
- [x] Igniting, flaming, smoldering, extinction, fuel consumption, and moisture
  response are continuous and covered at phase boundaries.
- [x] Lightning and lava require dry flammable loaded targets; nonflammable,
  saturated, unloaded, and protected cells do not ignite.
- [x] Fire work per tick is bounded; spread is deterministic; rain, fluid, and
  explicit suppression cool fire without unrelated world mutation.
- [x] Burned blocks use environment mutations and create bounded, persistent,
  recovering burn records that can be queried by later ecology stages.
- [x] Legacy `WXIMPACT1` payloads migrate and the new fire/burn payload rejects
  corruption without partially replacing runtime state.
- [x] Entity heat/smoke hazards, flame/smoke rendering, near haze, audio,
  quality budgets, DSL parsing, typed fields, inspect, and screenshot v10 pass.
- [x] A forced 1920x1080 Medium capture is nonblank and visibly shows flames and
  a wind-leaning smoke column; the report identifies fire and plume parameters.
- [x] `make test`, `make test-sanitize`, `make test-long-run`, `make test-e2e`,
  `git diff --check`, and the standard Medium performance route pass.
- [x] The codebase index is refreshed and Stage 04 is committed independently.

## Verification evidence

- Unit and property coverage: `test_wildfire_model`, `test_weather_impact`,
  `test_entity_replay`, `test_entity_ecology`, `test_environment_presentation`,
  `test_environment_runtime`, and `test_audio_environment` pass. Full normal
  and sanitizer gates each report `73 passed, 0 failed`; architecture and
  public-header checks pass. `make test-long-run` passes the deterministic
  multi-thousand-frame weather route.
- DSL/E2E: `make test-e2e` passes normal completion and stdin EOF without
  exiting, explicit `exit` return codes, batch failure handling, wildfire
  ignition/suppression/clear, typed fields, and screenshot schema 10. The
  wildfire capture is `screenshots/voxelcraft_20260819_094954_000.png` with
  report `screenshots/voxelcraft_20260819_094954_000.txt`: 1920x1080 Medium,
  nonblank, active igniting fire, flame tongues 3, smoke puffs 5, wind angle
  0.785398 radians, and plume drift 0.42.
- Standard Medium performance: `/tmp/voxelcraft-stage04-perf.keyvalue`,
  schema 3, `report_status=pass`, 720-frame route with 120 warmup and 600
  samples. CPU frame mean/p95 is 4.012/5.988 ms; GPU 0.448/0.624 ms;
  upload p95 0.079 ms; `sync_rebuilds=0`; queue, worker, and resource
  stability targets all pass. The route was executed against the active
  Hyprland display.
- `git diff --check` passes. The codebase-memory repository index was refreshed
  in full after implementation. `AGENTS.md` remains untracked and is excluded
  from the stage commit.

## Out of scope

Player health, firefighting inventory/tools, rigid multi-block tree collapse,
regional off-screen fire growth beyond persisted known cells, and full plant
succession are deferred. Stage 05 supplies additional burned/charred material
families, and Stage 06 consumes the burn-severity hook for regrowth.
