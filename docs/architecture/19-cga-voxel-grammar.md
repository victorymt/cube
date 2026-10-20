# CGA Voxel Grammars

## Scope

The world structure generator uses a deterministic integer voxel grammar (CGA)
for houses, mineshafts, dungeons, desert temples, and Chinese classical
courtyard complexes. Structure discovery,
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
| 5 | Home house | raised stone platform, white infill walls, red timber frame, carved doorway/windows, and stepped dark-tile roof |
| 2 | Mineshaft | diagonal air corridor with periodic timber supports |
| 3 | Dungeon | stone-brick perimeter, doorway, and internal supports |
| 4 | Desert temple | sandstone pedestal, pillars, capstone, and underground base |
| 6 | Chinese classical hall (retired intermediate stage) | five-bay hall with stone terrace, red columns, white plaster, lattice openings, painted bracket band, stepped hip roof, and ridge ornaments |
| 7 | Chinese classical courtyard complex | south-facing axial walled compound: three-bay gatehouse, forecourt with a paifang, ceremonial path, two side halls, covered front corridors, a low screen wall, a two-storey five-bay main hall on a raised terrace with a front porch lower eave, upper-storey windows/plaque, concave hip and gable roofs with upturned corners and exposed rafter/tile ends, two-tier painted bracket bands, gold-framed plaques, couplets, lattice openings, stone rails, pines, lions, and an incense burner |

The complex is generated on a 73×81 local footprint plus a ten-block cleared
margin. The planner samples a twelve-block apron around the site and uses its
highest terrain point as the terrace level, with a fourteen-block-deep stone
foundation covering the low corners. This keeps the courtyard visible on gently
sloping ground instead of sinking it into a hollow. Complexes use a 320-block
anchor spacing and reserve the site against house placement. Covered corridors
with lean-to tile roofs link the gate to the side halls; the main front eave
carries exposed rafter ends and a raised tile lip, and the front bracket band
has a second, lower tier of painted arms. Both hip and gable roofs paint
alternating tile ribs over the stepped courses and add a continuous outward
drip tile, a raised eave lip and spaced wood rafter ends; the hip corners use a
two-step connected upturn instead of a thin detached stack. The main hall is
carried to two storeys: a front porch lower eave at 13–14, an upper wall,
upper windows and plaque, and a second bracket band under the main hip roof at
eave level 18. This keeps the lower plaque/door in shadow under the porch while
the upper storey remains legible from the courtyard.

All grammars use world half-open AABBs and remain bounded by
`CGA_MAX_FEATURES_PER_CHUNK` and `CGA_MAX_CHUNK_OPS`. Expansion reserves its
operation budget before emitting records, so capacity failure cannot produce a
partial structure.

## Verification

- `make test` passes the complete 119-test suite, clean-build guard, and
  architecture checks.
- `build/normal/tests/test_cga_voxel` covers scope operations, all grammar
  expanders, rotations, operation ordering, and conflict order invariance.
- `build/normal/tests/test_terrain_scale` covers terrain suitability,
  underground section materialization, all planned grammar kinds, and
  deterministic structure baselines.
- `make test-cga-previews-e2e` starts the game with each debug DSL scene,
  streams the target region, captures a screenshot, and verifies a clean script
  exit. `cga_classical_complex.dsl` targets the default-seed complex near
  `(7680, 88, 3840)` (a south-facing axis; the gate opens toward +Z) and
  views the two-storey main hall from the inner courtyard. The scene uses
  `view distance 12`, a 240-frame streaming warm-up, cleared weather, and
  `world time 0.5` so the compound is captured at local noon.
  `world time auto` restores the simulated day cycle.

## Boundaries

The current `CgaContext` callback is an optional immutable occupancy hook;
production terrain generation uses the planner's deterministic in-workspace
conflict set and passes a null external context. Dynamic player edits and
runtime structure mutation remain outside procedural generation.
