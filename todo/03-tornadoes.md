# Stage 03: Tornado lifecycle, hazards, and presentation

## Purpose

Add rare, spatially coherent tornadoes to severe convective storms. A tornado
is a local moving phenomenon rather than a global weather flag: it has a world
position, continuous path, finite radius and height, intensity, lifecycle, and
distance-dependent effects. Natural formation must be meteorologically
eligible and deterministic, while debug forcing must make every state directly
testable without changing saved weather or climate data.

## Meteorology and lifecycle

- Natural formation requires an active atmosphere and water cycle, a
  cumulonimbus-dominated severe storm, warm/moist low-level air, strong
  instability, low pressure, and wind shear represented by the gap between
  sustained wind and gusts. Each condition contributes to a bounded formation
  potential; a missing essential condition prevents formation.
- Evaluate formation on deterministic fixed ticks and spatial storm cells.
  The same seed, world position, weather sample, and time produce the same
  decision. Natural rarity is separate from eligibility so debug forcing does
  not weaken the physical thresholds.
- Use four phases: `forming`, `intensifying`, `mature`, and `dissipating`.
  Intensity follows a continuous envelope across the phases. Radius, funnel
  height, maximum tangential wind, translation speed, dust loading, and damage
  potential are derived from intensity rather than changing discontinuously.
- The vortex follows the storm motion with a small deterministic meander. Its
  path is continuous across ticks. It does not teleport to follow the player.
  Only one local tornado is simulated in this stage, which keeps ownership and
  performance explicit while still allowing a complete realistic event.
- Runtime tornado state is never serialized. Surface transitions, weather
  suspension, explicit clear, or a changed surface ID reset the event.

## Local force field

- Sample a Rankine-like vortex around the moving center. Tangential speed grows
  through the core, decays outside it, and is combined with a weaker radial
  inflow and an updraft concentrated near the core. All forces taper smoothly
  to zero at the influence radius and funnel top.
- Players and entities receive acceleration, not direct position changes.
  Exposure depends on horizontal/vertical distance, shelter, immersion, mass
  proxy, armor, grounded state, and tornado intensity. Speeds are capped to
  preserve collision stability. The force query is a pure function so tests
  can verify direction, continuity, falloff, and bounds.
- Entities can take debris/wind damage in the damaging core. Existing weather
  temperature, hail, and lightning hazards remain independent.

## Terrain damage and debris

- Damage is enabled only when the existing `weather damage` setting is on.
  Each fixed tornado tick samples a small deterministic set of columns near the
  vortex. It never scans the whole world or every block in a radius.
- A sampled exposed block is removed only when local wind exceeds its material
  wind resistance. Bedrock, liquids, unloaded cells, and blocks below the
  exposed surface are protected. Damage uses the environment mutation source
  and increments dedicated tornado statistics.
- Every removed block emits a neutral styled particle carrying its material
  color and vortex velocity. Dust and lightweight debris emissions have strict
  per-tick and global effect-queue budgets. Visual debris is cosmetic and is
  not persisted as an entity.

## Presentation and audio

- Render the funnel as an unframed world-space tapered, rotating stack whose
  base follows the terrain and whose top reaches the cumulonimbus base. Width,
  opacity, lean, rotation, condensation reach, and dust skirt respond to phase,
  intensity, humidity, and surface proximity.
- Emit bounded dust and debris particles near the base. Precipitation veil and
  low-level dust increase when the camera is inside the circulation, while the
  tornado remains visible from outside it.
- Add a synthesized low-frequency tornado roar to the ambient mixer. Loudness
  follows smooth distance attenuation, intensity, exposure, and sheltering;
  it is silent for inactive tornadoes, underwater views, and non-surface
  scenes. Existing rain, wind, and thunder remain independently mixed.
- Graphics quality controls funnel segment count and emission rate. Performance
  mode keeps the deterministic simulation and rendering but disables persistent
  terrain damage, matching the existing weather-impact policy.

## Debug and inspection

- Add `weather tornado force INTENSITY FRAMES [DISTANCE]` to create a tornado
  downwind of the player. `INTENSITY` is 0-1, `FRAMES` is 1-36,000, and optional
  `DISTANCE` is 8-160 world units. Forcing bypasses eligibility and rarity but
  uses the normal lifecycle, force field, renderer, audio, and damage paths.
- Add `weather tornado clear` without clearing separately forced weather or
  cloud state. `weather clear` clears all three forcing channels.
- `weather inspect`, typed DSL values, and screenshot reports expose active,
  forced, phase, center, distance, intensity, radius, funnel height, wind,
  remaining frames, blocks damaged, debris emitted, and dropped effects.
- Screenshot report schema advances from version 8 to version 9.

## Tests and acceptance

- [x] Formation potential is bounded, deterministic, rejects ineligible
  environments, and accepts a physically suitable supercell sample.
- [x] Phase transitions and motion are continuous and deterministic; forced
  duration expires and clear/reset behavior is isolated from other forcing.
- [x] Force direction, core/exterior falloff, vertical taper, sheltering, and
  maximum acceleration/speed bounds are covered by unit tests.
- [x] Terrain work is bounded per tick; resistant/protected blocks survive;
  weak exposed blocks can be removed and emit budgeted debris.
- [x] Player and entity integration applies local force and damage only in the
  active influence volume.
- [x] Funnel, dust skirt, precipitation occlusion, distance-attenuated audio,
  DSL parsing, inspection, typed fields, and screenshot v9 are covered.
- [x] A forced 1920x1080 Medium capture is nonblank and visibly shows the
  tornado; the report identifies the event and its parameters.
- [x] `make test`, `make test-sanitize`, `make test-long-run`, `make test-e2e`,
  `git diff --check`, and the standard Medium performance route pass.
- [x] The codebase index is refreshed and Stage 03 is committed independently.

## Acceptance evidence

- Final visual capture:
  `screenshots/voxelcraft_20260819_085223_000.png` with report
  `screenshots/voxelcraft_20260819_085223_000.txt`.
- Capture validation: 1920x1080, 5,107 sampled colors, luminance 0-255,
  screenshot schema 9, Medium quality, mature forced tornado at intensity
  0.953, 46.3 world units away, 12.5-unit core radius, 75.5-unit funnel, and
  84.7 m/s maximum tangential wind.
- Standard Medium performance report:
  `/tmp/voxelcraft-stage03-perf.keyvalue`, schema 3, status `pass`, CPU frame
  mean/P95 4.019/5.894 ms, GPU frame mean/P95 0.450/0.636 ms, upload P95
  0.069 ms, final estimated mesh bytes 189,751,584, and all queue, worker,
  resource-stability, and synchronous-rebuild targets passed.

## Out of scope

Multiple simultaneous tornadoes, forecast/radar UI, mesocyclone-scale cloud
rotation, persistent tornado tracks across unloaded regions, and structural
multi-block rigid-body debris are later extensions. Wildfire receives its own
stage so fire weather, fuels, crown fire, smoke, and suppression can be designed
as one coherent system rather than folded into tornado damage.
