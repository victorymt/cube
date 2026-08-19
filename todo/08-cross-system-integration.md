# Stage 08: cross-system integration, migration, and performance audit

## Purpose

Close the remaining integration gap after spherical topology: weather,
ecology, wildfire, fluid, tornado, save/load, and rebase queries must agree on
one surface identity. A longitude alias or pole reflection must not create a
second climate field, ecology cache entry, population region, or impact key.

## Plan

- Canonicalize weather field coordinates through `SurfaceCanonicalMapCell` at
  the public world-query boundary. Preserve the local frame's reflected north
  sign in the canonical map position so wind and latitude remain physically
  meaningful while equivalent cells sample the same field.
- Canonicalize ecology local queries, deterministic hashes, population regions,
  and evolution lookup coordinates with the same topology helper. Keep raw
  coordinates only at rendering/chunk boundaries where a local frame is
  explicitly required.
- Verify wildfire and fluid canonicalization still uses the same surface cell;
  add cross-system tests for seam aliases, pole reflections, cache identity,
  and deterministic save/load replay.
- Add read-only DSL inspection for the canonical weather/ecology cell and run
  focused, full, sanitizer, long-run, graphical E2E, and performance gates.

## Acceptance

- [x] Weather samples match at longitude and both pole aliases, including
  cloud genus, precipitation, temperature, humidity, and wind vector semantics.
- [x] Ecology local state, deterministic flora hashes, population region keys,
  wildfire impact keys, and fluid edits resolve to one canonical cell.
- [x] Save/load and a simulated origin rebase preserve canonical identity and
  do not duplicate runtime records.
- [x] DSL reports canonical weather/ecology identity without mutating gameplay.
- [x] Focused tests, `make test`, sanitizer/long-run/E2E/performance gates,
  `git diff --check`, and a refreshed codebase-memory index pass.

## Verification evidence

- Focused weather and ecology suites passed with deterministic longitude,
  north-pole, and south-pole alias checks. Weather compares the complete field
  sample, while ecology compares local ecology, population/cache results, and
  evolution-region state. Existing wildfire and fluid suites retained their
  canonical identity and load-migration coverage.
- The normal and ASan/UBSan gates each passed 75 tests with 0 failures. The
  long-run weather gate passed, and the chunk benchmark passed with median
  `8.844 ms`, P95 `9.416 ms`, `mesh_snapshot_bytes=8192`, and
  `sync_rebuilds=0`.
- The retained graphical E2E route passed with
  `gallery=validated topology=validated captures=5`. It asserted identical
  weather/ecology cells and 64-block ecology regions after the longitude,
  north-pole, and south-pole rebases, retained zero duplicate canonical chunks,
  and captured five nonblank 1920x1080 frames under Hyprland. DSL stdin errors
  still stop only the current block, batch errors exit 3, and explicit
  `exit N` retains its requested process status.
- The standard Medium performance route passed schema 3 with 720 frames,
  120 warmup frames, and 600 samples. CPU frame mean/P95 were `6.672 ms` and
  `10.303 ms`; GPU frame mean/P95 were `0.429 ms` and `0.460 ms`.
  Generation and mesh jobs completed `1339/1339` and `2356/2356`, final pending
  mesh snapshot bytes were zero, and all queue, worker, resource-stability, and
  synchronous-rebuild targets passed.
- `git diff --check` passed. The codebase-memory index was refreshed after the
  final implementation and verification, and the independent commit excludes
  `AGENTS.md`, saves, screenshots, build products, and `/tmp` reports.

## Limits

This stage does not introduce new cloud genera, flora taxa, or a cube-sphere
renderer. It integrates the existing models and keeps save schemas unchanged
unless an actual identity migration requires a version bump.
