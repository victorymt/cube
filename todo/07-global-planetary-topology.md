# Stage 07: Global planetary topology and seamless traversal

## Purpose

Make every solid-surface world a finite, globally traversable sphere. Homeworld
and suitable generated/real planets must agree at the longitude seam and at
both poles for terrain, climate, lighting, physics-facing surface frames,
streaming identity, edits, and map navigation. Raw local coordinates may remain
useful to the renderer, but they are never the persistence identity.

## Topology contract

- `SurfaceProjectMapCoordinates()` is the single continuous projection. It
  wraps longitude, reflects latitude across either pole, reports the tangent
  north sign, and is finite for arbitrary float input.
- Add exact integer-cell canonicalization for surface columns and a stable
  canonical hash key. Equivalent coordinates such as one equatorial period or
  a pole reflection resolve to the same surface identity; radial/block Y stays
  part of the key.
- Add shortest wrapped/reflected map offsets for camera, marker, and debug
  queries. No consumer may compare unbounded raw `x/z` to decide whether two
  surface locations are the same.
- Surface patch transforms use canonical sphere frames for both reference and
  patch positions. A patch crossing longitude or a pole must have continuous
  position, tangent basis, and normal orientation; high-latitude spacing is
  allowed to converge physically instead of pretending the map is flat.
- Homeworld and other solid planets use direction/sphere-based procedural
  noise for all global terrain/climate signals that previously used local 2-D
  noise. The same direction produces the same sample after wrapping or pole
  reflection.

## Runtime and persistence

- Chunk lookup, generation completion, mesh upload, and dirty-neighbor checks
  use canonical surface addresses in spherical worlds. A raw coordinate alias
  cannot allocate a duplicate chunk or discard its canonical neighbor.
- Block edits keep their legacy raw coordinates for compatibility but index and
  query by canonical surface address. Save trailer schema 2 records the
  canonical address and migrates schema 1 after validating the raw coordinate.
  Loading is transactional and rejects mismatched body/radial identities.
- Map markers canonicalize surface coordinates on create/load and use the
  spherical distance/bearing path for navigation. Existing marker records are
  migrated without changing IDs or names.
- Fluid, weather-impact, wildfire, tornado, ecology, and entity consumers must
  not invent a second topology. Their loaded-surface queries use the same
  canonical identity or an explicitly documented local-frame conversion.
  Bounded runtime work and save formats remain unchanged unless a versioned
  migration is required.
- Player traversal keeps velocity/yaw tangent when crossing a pole and may
  periodically rebase whole periods to keep float precision bounded. Rebase is
  an explicit world event so loaded chunks, markers, effects, and debug reports
  remain coherent.

## Debug and tests

- Add deterministic topology tests for longitude aliases, both pole
  reflections, exact integer-cell identity, shortest offsets, frame continuity,
  canonical noise, and schema-1/schema-2 save migration.
- Add DSL inspection for `world topology`, reporting body, canonical longitude
  and latitude, north sign, wrapped aliases, loaded canonical chunk count, and
  the last rebase. It is read-only and does not alter normal gameplay.
- Extend the E2E route to walk across the longitude seam, cross north and south
  poles, edit an aliased block, place/query a marker, and save/load. The route
  must report no duplicate canonical chunks, no terrain seam, and no lost edit.

## Acceptance

- [x] Stage document is complete before implementation and records exact test
      evidence before commit.
- [x] Surface topology and spherical noise unit tests pass, including finite
      arbitrary-coordinate properties.
- [x] Homeworld and at least two suitable planets match at wrap/pole aliases
      for terrain height, biome/climate, bathymetry, and surface frame.
- [x] Streaming and mesh upload never retain duplicate canonical chunks across
      aliases; seam/pole neighbors remain drawable within the bounded budget.
- [x] Edits, markers, fluid/weather state, and save/load preserve canonical
      identity through traversal and schema migration.
- [x] DSL topology inspection and traversal E2E pass; screenshot remains
      nonblank at 1920x1080 under Hyprland.
- [x] Focused tests, `make test`, sanitizer/long-run/E2E gates, performance
      route, `git diff --check`, and a refreshed codebase-memory index pass.
- [x] Commit contains only Stage 07 files and excludes untracked `AGENTS.md`.

## Verification evidence

- The topology suite covers longitude aliases, both pole reflections, exact
  integer-cell identity, shortest wrapped offsets, frame continuity, finite
  arbitrary inputs, and canonical spherical noise. Save tests cover schema 1
  migration, schema 2 canonical addresses, transactional rejection, marker
  migration, and spherical player/entity state.
- The full normal test gate passed 75 tests with 0 failures, including public
  headers, module archives, clean-build guards, and architecture checks. The
  sanitizer gate passed the same 75 tests with ASan/UBSan; the long-run weather
  test and chunk benchmark also passed. The benchmark reported median `8.889
  ms`, P95 `9.119 ms`, `mesh_snapshot_bytes=8192`, and `sync_rebuilds=0`.
- The retained graphical E2E route passed with
  `gallery=validated topology=validated captures=5`. It exercised the
  longitude seam, aliased edits, marker save/load, both pole rebases, deep
  water, wildfire, DSL error/exit contracts, and zero-issue stream audits.
  The north and south pole reports both showed `duplicate_canonical_chunks=0`;
  the screenshot capture was nonblank at 1920x1080 in the active Hyprland
  session.
- The standard Medium performance route passed schema 3 with 720 frames,
  120 warmup frames, and 600 samples. CPU frame P95 was `10.170 ms`, GPU
  frame P95 was `0.456 ms`, generation and mesh jobs completed `1330/1330`
  and `2352/2352`, final pending mesh snapshot bytes were zero, and
  `sync_rebuilds=0`. The route rendered `155025` visible section candidates
  (`solid_draws=421`, `water_draws=11634`) and passed all baseline/resource
  targets. Ocean waypoints are clamped to sea level so the route remains in
  the valid streamed vertical range.
- The codebase-memory index was refreshed after implementation and
  verification. This stage is committed independently without `AGENTS.md`,
  generated screenshots, save files, or performance artifacts.

## Out of scope

Cube-sphere visual remeshing, pole-specific gameplay biomes, floating-point
precision beyond the bounded rebase contract, and new weather/biology models
remain Stage 08 integration work when they are not required to preserve the
canonical identity established here.
