# Stage 09: renderer, mesh, and simulation performance optimization

## Purpose

Turn the performance audit into measured, behavior-preserving optimizations
for the chunk mesh pipeline, render submission, entity/environment updates,
and UI text rendering. The work follows the principles used by Sodium,
Lithium, FerriteCore, ImmediatelyFast, EntityCulling, and spark, adapted to
the existing C/ raylib architecture.

This stage is implementation work, not a promise of a fixed FPS number. Every
optimization must first identify the affected metric, preserve the existing
world/simulation contracts, and be accepted on the same deterministic route.

## Scope

- Stage-level performance counters and reproducible debug/benchmark routes.
- Reuse of section snapshots and flora ownership data during mesh building.
- Event-driven dirty-section scheduling with distance/LOD priority.
- Cached render metadata, conservative visibility filtering, and material
  batching where the renderer can support it without changing transparency.
- Dense active-entity iteration and spatial broad-phase checks for hazards.
- UI text buffering only if the measured render path makes it worthwhile.

## Non-goals

- Do not change block, flora, fluid, weather, tornado, wildfire, or save
  semantics to obtain a benchmark improvement.
- Do not blindly shrink block types or share mutable mesh/GPU buffers between
  worker jobs.
- Do not make EntityCulling-style visibility affect gameplay simulation.
- Do not add a new debug mechanism when the existing DSL and `--debug-trace`
  fields can expose the required state.

## Baseline route

Use the same seed, resolution, worker count, render distance, player position,
and fixed debug clock for every comparison. The route must wait for both
`stream.surface_ready` and `stream.audit_complete` before collecting samples.

```sh
make -s benchmark-chunks
make -s test
build/normal/voxelcraft --debug-resolution 1920x1080 \
  --chunk-workers 4 --debug-trace /tmp/perf-stage.jsonl \
  --debug-script /tmp/perf-stage.dsl
```

The DSL route should use `start`, `wait until game.screen == "playing"`, a
fixed `teleport`, `stream wait`, the two stream assertions, `status`, and an
explicit `exit 0`. Compare at least three benchmark runs on the same host.
Use median, P95, max/spike samples, main-thread CPU, worker CPU, upload CPU,
draw calls, queue peaks, snapshot bytes, and final/peak resource bytes.

Current reference observed during planning:

| Metric | Value |
| --- | ---: |
| `median_total_ms` | 35.969 |
| `p95_total_ms` | 36.250 |
| `mesh_snapshot_bytes` | 8192 |
| `solid_vertices_per_chunk` | 2187.8 |
| `solid_vertex_reduction_percent` | 68.5% |
| `sync_rebuilds` | 0 |

The benchmark's `mesh_snapshot_bytes` measures the benchmark input snapshot;
it does not include the temporary flora-stripped arrays created by the runtime
mesh helpers.

## Implementation stages

### 09.1: instrumentation and baseline schema

**Files:** `src/core/perf.*`, `src/app/game_debug_trace.*`,
`src/world/chunks_runtime.*`, `src/world/chunks_mesh_job.c`,
`src/presentation/render_scene.c`, `src/presentation/entity_renderer.c`,
`src/ecology/entity.c`, `docs/debug-dsl.md`, focused performance tests.

- Add bounded counters/timers for snapshot copy, boundary capture, solid,
  water, flora, and LOD mesh builds.
- Add render collection, shadow collection, entity draw, hazard, fluid, and
  UI text timing fields only where the existing trace/report architecture can
  carry them without per-frame allocation.
- Add culling hit/miss, material/state changes, and batch counters.
- Keep report schema versioned and update the baseline reader/tests.

**Acceptance:** the deterministic route produces all new fields; existing
baseline fields remain compatible; no timing path allocates or logs per frame.

**Commit:** `perf: add stage-level optimization telemetry`

### 09.2: mesh snapshot reuse and flora owner map

**Files:** `src/world/chunks_flora.*`, `src/world/chunks_mesh_job.c`,
`src/world/chunks_internal.h`, `tests/benchmark_chunks.c`, focused mesh tests.

- Build one flora-stripped block snapshot per exact section job and pass it to
  solid, water, and flora emitters.
- Replace repeated full-section `FloraStructureOwnerAt` lookups with one
  owner map or bounded per-structure emission while preserving structure
  precedence and block matching.
- Keep raw blocks available for flora structure rendering and preserve the
  existing `MeshJob` lifetime/thread ownership rules.
- Measure boundary capture separately; defer boundary caching to a follow-up
  substage unless
  telemetry shows it dominates.

**Acceptance:** mesh vertices, water/flora output, structure precedence,
`mesh_snapshot_bytes=8192`, and `sync_rebuilds=0` remain unchanged; benchmark
median/P95 does not regress by more than 5% and the new copy/owner counters
show a reduction on a flora-heavy fixture.

**Commit:** `perf: reuse section mesh snapshots`

### 09.3: dirty-section priority queue

**Files:** `src/world/chunks_storage.*`, `src/world/chunks_runtime.*`,
`src/world/chunks_streaming.*`, `src/world/chunks_internal.h`,
`tests/test_chunk_streaming.c`, `tests/test_terrain_scale.c`.

- Enqueue dirty sections at the existing dirty-marking boundary.
- Deduplicate by chunk slot, section Y, dirty stamp, and target LOD.
- Prioritize player-near exact work, then visible/near LOD work, then preload
  work; retain the existing submission and upload budgets.
- Discard stale queue entries after edits, unloads, rebases, or LOD changes.
- Retain a bounded fallback scan for recovery and test fixtures.

**Acceptance:** no dirty section is lost, stale jobs cannot clear a newer dirty
stamp, neighbor invalidation tests pass, and idle frames no longer scan all
active chunks merely to discover that nothing is dirty.

**Commit:** `perf: schedule dirty mesh work by priority`

### 09.4: render metadata, visibility, and batching

**Files:** `src/presentation/render_scene.c`, `src/presentation/render_sort.*`,
`src/presentation/world_renderer.c`, any new render batch module,
`tests/test_render_sort.c`, focused renderer tests, `docs/terrain-performance.md`.

- Cache section render metadata and non-spherical transforms; use a coarse
  chunk bound before section-level sphere tests.
- Avoid recomputing reusable metadata between main and shadow passes while
  keeping pass-specific frusta correct.
- Batch solid/flora submissions by shader/material/atlas and culling mode.
- Keep water and other transparent objects back-to-front; only replace qsort
  after a correctness test proves the alternative ordering equivalent.
- Never change spherical canonicalization, two-sided flora behavior, or water
  blending as a side effect of batching.

**Acceptance:** renderer tests retain ordering and visual contracts; graphical
E2E passes under Hyprland; draw-call/state-change counters improve in a dense
scene without increasing GPU P95 or upload spikes.

**Commit:** `perf: cache visible sections and batch world draws`

### 09.5: active entities and hazard broad phase

**Files:** `src/ecology/entity.*`, `src/presentation/entity_renderer.*`,
`src/world/weather_impact.*`, `src/world/tornado.*`,
`tests/test_entity_ecology.c`, `tests/test_weather_impact.c`,
`tests/test_tornado.c`.

- Maintain a dense active-entity index while keeping stable entity storage and
  replay/save indices.
- Apply cheap distance/bounds visibility tests before expensive entity drawing;
  optionally compute conservative occlusion data asynchronously for rendering
  only.
- Use chunk/region broad-phase candidates for tornado and wildfire exposure.
- Reuse per-tick shelter/fluid samples where the simulation contract permits;
  retain deterministic fixed-step behavior and death ordering.

**Acceptance:** entity replay, ecology, weather, tornado, wildfire, and save
tests remain deterministic; hidden entities are not drawn; active/candidate
counts and hazard CPU P95 improve in stress fixtures.

**Commit:** `perf: use active entity and hazard candidates`

### 09.6: conditional UI text buffering

**Files:** `src/presentation/render_ui.*`, render UI tests, performance docs.

- Only proceed if 09.1 shows UI text is material to frame P95.
- Cache static HUD strings and batch glyphs by font/atlas; preserve the current
  shadow-then-foreground order and Unicode/codepoint behavior.
- Avoid a new cache for rapidly changing text unless an eviction bound and
  invalidation rule are defined.

**Acceptance:** UI screenshots and text tests are unchanged, allocation count
  is bounded, and text-heavy scenes show lower render CPU/draw calls.

**Commit:** `perf: buffer repeated UI text` (only if the stage gate is met)

### 09.7: final verification and index refresh

**Files:** the relevant stage document, `docs/terrain-performance.md`,
`docs/debug-dsl.md`, and performance baseline artifacts only.

- Run focused tests after each implementation stage.
- Run `make test`, `make test-sanitize`, `make test-long-run`,
  `make test-e2e`, and `make test-debug-graphics-e2e` according to the
  affected systems.
- Run the three-sample chunk benchmark and the fixed DSL performance route.
- Compare median, P95, max spikes, draw/state counters, worker queues,
  snapshot/resource peaks, and semantic invariants.
- Refresh the codebase-memory index after the final code changes.
- Update this document with exact evidence, known limits, and residual risks.

**Acceptance:** all applicable gates pass, no baseline invariant regresses,
and every implementation stage has its own focused commit.

**Commit:** `docs: record performance optimization evidence`

## Rollback and risk rules

- If a stage increases P95 or upload spikes by more than 5% on the same route,
  stop that stage and retain the telemetry for diagnosis.
- If a visual or simulation invariant fails, revert only the stage commit; do
  not mask the failure by weakening tests or changing the baseline.
- Async visibility must fail open (render conservatively) when data is stale,
  unavailable, or a whitelist entry is missing.
- Queue changes must preserve dirty stamps and job cancellation semantics.

## Completion evidence

The canonical index, priority queue, and worker-local scratch-buffer stages
were committed independently as `28362db`, `bc71320`, and `813d0e9`. The
current benchmark preserves the required snapshot and synchronous-rebuild
invariants; see `docs/architecture/09-performance-optimization.md` for the
measured samples and residual graphics E2E limitation. The roadmap marks this
stage complete; future renderer batching remains conditional on new telemetry.
