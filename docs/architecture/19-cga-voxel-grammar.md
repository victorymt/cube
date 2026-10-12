# CGA Voxel Grammars

## Scope

The world structure generator uses a deterministic integer voxel grammar (CGA)
for houses, mineshafts, dungeons, and desert temples. Structure discovery,
rule expansion, conflict resolution, and chunk materialization are separate
steps. The implementation lives in `src/world/cga_voxel.c` and its public
contract is `src/world/cga_voxel.h`.

## Data flow

1. `CgaPlanStructureFeatures` enumerates anchor cells around the requested
   chunk and emits bounded `CgaFeature` records.
2. Terrain suitability and biome predicates reject invalid candidates before
   expansion. House footprints allow at most one block of height variation.
3. Candidates are sorted by their deterministic `feature.id`. Intersecting
   footprints keep the smaller ID, so chunk load order and candidate traversal
   order do not change the result. `CgaContext.regionClear` can add an
   immutable external occupancy predicate.
4. `CgaExpandFeature` lowers each grammar version to fixed-capacity
   `CgaVoxelOp` records. Operations are ordered by stage, priority, feature ID,
   and sequence.
5. `CgaEvaluateChunk` clips each operation to the target chunk and applies the
   resulting writes. A mineshaft first materializes its underground terrain
   sections so carving has a generated base to modify.

## Grammar versions

| Version | Structure | Rule output |
| --- | --- | --- |
| 1 | Home house | plank shell, carved interior, door, windows, wood floor and roof |
| 2 | Mineshaft | diagonal air corridor with periodic timber supports |
| 3 | Dungeon | stone-brick perimeter, doorway, and internal supports |
| 4 | Desert temple | sandstone pedestal, pillars, capstone, and underground base |

All four grammars use world half-open AABBs and remain bounded by
`CGA_MAX_FEATURES_PER_CHUNK` and `CGA_MAX_CHUNK_OPS`. Expansion reserves its
operation budget before emitting records, so capacity failure cannot produce a
partial structure.

## Verification

- `make test` passes the complete 119-test suite, clean-build guard, and
  architecture checks.
- `build/normal/tests/test_cga_voxel` covers scope operations, all grammar
  expanders, rotations, operation ordering, and conflict order invariance.
- `build/normal/tests/test_terrain_scale` covers terrain suitability,
  underground section materialization, all four planned grammar kinds, and
  deterministic structure baselines.
- `make test-cga-house-e2e` starts the game with the debug DSL, streams the
  target region, captures a screenshot, and verifies a clean script exit.

## Boundaries

The current `CgaContext` callback is an optional immutable occupancy hook;
production terrain generation uses the planner's deterministic in-workspace
conflict set and passes a null external context. Dynamic player edits and
runtime structure mutation remain outside procedural generation.
