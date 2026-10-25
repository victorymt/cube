# world type decoupling and rebuild-radius work (2026-10-24 to 2026-10-25)

- Baseline before this work: `743ebae`
- Method: measured include graph from `build/normal/obj/**/*.d`, real rebuild radius (`touch <header>` then `make -j8 | grep -c "^cc "`, 235 objects total), mutation tests against every new architecture rule
- Nature: **implementation record**. It records what the phases achieved and, just as importantly, which planned targets were **not** met and which phase was **abandoned after measuring it**.

## Why

Record 22 split `world/world.h` by responsibility and cut its rebuild radius from 87 to 19. It did not touch the type layer underneath: `world/block_types.h`, `world/block_edits.h` and `world/world_state.h` still all included `world/world_types.h`, so the split moved declarations out of an aggregate while every consumer still pulled the same 576-line type header.

The cost was rebuild time, not correctness. `world/world_types.h` alone rebuilt 150 of 235 objects, and `world/surface_topology.h` 169 — more than `raylib.h` (187) reached by any module that only stored a surface key.

## Phase results

| Phase | Change | Target | Measured result |
| --- | --- | --- | --- |
| 1 | `core/config.h` no longer names `TEX_COUNT`; `ATLAS_ROWS_FOR(n)` takes it as a parameter | core has no domain symbol | met, mutation-verified |
| 2 | `world_types.h` split into `block_type.h`, `block_texture.h`, `flora_types.h`, `chunk_types.h`, `block_edit_types.h`, `terrain_types.h`; aggregate deleted and banned | `block_type.h` ≤ 60 | **not met: 143** |
| 3 | `surface_types.h` (POD only, raylib-free) split out of `surface_topology.h` | `surface_topology.h` ≤ 90 | **not met: 169 → 145** |
| 5 | `debug_dsl.c` and `game_debug.c` split at their real seams | no file near the 2100-line limit | met: 2097 → 1157, 2077 → 1838 |
| 4 | 11 `TerrainTest*` declarations moved to `terrain_test.h` | `terrain.h` ≤ 45 | **not met: 174 → 156** |
| 6 | break the `world ↔ space` include cycle | cycle shrinks | **abandoned: it grew from 11/9 to 12/11** |

Current rebuild radius, measured after every phase:

| Header | Objects rebuilt (of 235) | Note |
| --- | ---: | --- |
| `world/surface_types.h` | 156 | new POD header; sits on the `chunk_types.h` chain |
| `world/surface_topology.h` | 145 | was 169 |
| `world/block_type.h` | 143 | was inside a 150-object aggregate |
| `world/terrain_types.h` | 128 | |
| `world/chunk_types.h` | 125 | |
| `world/flora_types.h` | 125 | |
| `world/terrain.h` | 91 | was 91 before the test-seam move; the seam move did not change this |
| `world/block_texture.h` | 70 | |
| `world/block_edit_types.h` | 53 | |
| `world/world_types.h` | — | deleted; a rule now fails the build if it returns |

## Why two targets were missed

`block_type.h` is 143 despite having **no includes at all**. The reason is that a header's rebuild set is decided by its *consumers'* other includes, not by the header itself: 30 files include `block_type.h` directly `<!-- volatile-counts:allow -->`, and most of them also need `chunk_types.h` or `terrain_types.h`. Touching `block_type.h` therefore rebuilds the union of those chains.

The same explanation applies to `surface_types.h` (156): it is a leaf, but it is reachable from `chunk_types.h` and `block_edits.h`, which most of `world` includes.

Reaching the original targets would require splitting `chunk_types.h` itself — separating `Chunk` from `ChunkSection` from the generation-job types — and that header's largest consumer is the chunk storage layer, which needs all of them together. That is a different, larger phase with no measured payoff proven in advance; it is not part of this record.

`terrain.h` stayed at 91 because its cost is driven by its *declarations*, not by the test block: the 52 production functions need 14 types between them, including `Chunk`, `Vector3`, `PlanetProfile`, `PlanetSurfaceSample` and `FloraTaxonId`. Splitting by family produces headers that each still drag in nearly the whole closure.

## Why phase 6 was abandoned

The planned move was to lift `VolcanoTerrainSample` and `PlanetSurfaceSample` into a neutral `world/surface_sample.h` so `world/terrain.h` would stop including `space/planet_surface.h`.

It does not work, because the dependency is not only a type dependency. Thirteen functions that `terrain.h` declares — `PlanetSurfaceAtTime`, `PlanetSurfaceBaselineAt`, `PlanetBiomeAt`, `PlanetTerrainSampleAt` and their neighbours — are **implemented inside `world/`** (`terrain_planet_sample.c`, `terrain_planet_height.c`, `terrain.c`). Their declarations have to stay in `world`, and their implementations have to call `space` code, so `world → space` remains. The measured cycle went from `11 vs 9` to `12 vs 11`: moving the types added includes without removing the functional edge.

The change was reverted in full. The cycle is a real structural fact — `space` owns the planet model and `world` owns the terrain that samples it — not an artefact of header placement.

## What about `core/config.h`

`core/config.h` used to name `TEX_COUNT`, which is defined in the block-texture header. That was a genuine inversion: `core` may not depend on any higher component, and it compiled only because of include order. `ATLAS_ROWS_FOR(textureCount)` moves the knowledge to the caller.

`tests/test_architecture.sh` now rejects domain symbols in `core` outright. The rule strips whole-line comments before scanning, so prose about blocks is still allowed while `#define ATLAS_ROWS TEX_COUNT` is not.

## Guards added

Four rules, each mutation-tested by reintroducing the violation and confirming the check fails:

- `world_types.h` may not exist ("include the focused header you need").
- `surface_types.h` may not include `raylib.h`, `raymath.h` or `rlgl.h`.
- `core/` may not reference `TEX_*`, `BLOCK_*`, `Chunk*`, `Surface*` or `BIOME_*` symbols (`BLOCK_SIZE` exempt).
- Public headers may not include `_internal.h` — this one already existed and rejected the first attempt to place `GameDebugDispatchResult` in a private header.

## Known limits

- The rebuild numbers are from one host and one compiler; the ranking is stable but the absolute counts move with `mk/tests.mk`.
- `block_type.h` and `surface_types.h` remain high for a structural reason, not an oversight. Treat their numbers as a baseline, not a target.
- `src/ecology/ecology_population.c` was evaluated for splitting and deliberately left at 2028 lines `<!-- volatile-counts:allow -->`: its 28 `EcologyEvolution*` functions are not contiguous and reference 55 non-evolution internal symbols, so a split would require exporting most of the file's internals.
- No visual or GPU verification was involved. Every claim here is a build-graph measurement, a compiler rule, or a test result.

## Related

- [record 21: coupling review](21-coupling-review-2026-10-24.md) — the measurements that motivated this work
- [record 22: world.h split](22-world-header-split-2026-10-24.md) — the declaration-level split this record extends to types
- [documentation rules](../documentation-rules.md) — the gate that keeps this record honest about its own dates
