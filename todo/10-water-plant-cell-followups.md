# Water/plant cell model stages 3–5

> **执行状态入口（2026-11-09）**：本文件保留为详细行为规格。新的
> [Homeworld cube migration execution plan](../docs/cube-migration-execution-plan.md)
> 决定顺序和状态：Stage 5 已随 M3 完成，Stage 3–4 分别归入 M4.3 和 M4.4。

## Purpose

This plan records the deferred ecological, generation-order, and persistence work that follows the cell-volume water refactor. "Stage 3", "Stage 4", and "Stage 5" are local to the water/plant cell model and do not refer to the long-term Stage 03 tornado record or another numbered roadmap item. Current state is maintained in [STATUS.md](../STATUS.md).

## Sequencing

The stages run in order because each one establishes an input for the next. Stage 3 defines how plants respond to water occupancy, Stage 4 makes generation produce worlds that satisfy those rules, and Stage 5 stores and exposes the resulting cell state without losing either half of the cell. Implement, test, document, and commit each stage separately.

## Stage 3: plant/water ecological coupling

**Execution status:** complete in M4.3. The behavior and acceptance rules below
remain the detailed specification.

### Goal

Use water volume rather than `BLOCK_WATER` block identity for plant flooding, while keeping every plant anchored to its supporting substrate.

### Work

- Add a deterministic flood-tolerance classification to flora traits: submerged-intolerant terrestrial plants, saturation-tolerant wetland plants, and aquatic or submerged plants.
- Track bounded flood exposure and recovery for each affected plant structure or cell, scaled by simulation time, water volume, and tolerance class.
- Apply damage, death, and succession only after the declared tolerance window is exceeded; preserve deterministic ordering and do not let rendering or off-screen state change the outcome.
- Keep plants attached to their substrate when water shares the cell; water must not move, dislodge, or float a plant.
- Audit water tests in player, fauna, flora, environment, and spawn paths so liquid state comes from volume-aware helpers rather than a direct `GetBlockAt(...) == BLOCK_WATER` comparison.
- Exclude solid blocks from water occupancy and define the expected behavior at partially filled cells and cell boundaries.

### Acceptance

- Focused tests cover dry, saturated, briefly submerged, and chronically submerged cases for every tolerance class.
- Tests prove that flood damage begins at the documented threshold, recovery is deterministic, and dead plants enter the existing succession path without duplicating biomass or structures.
- Anchoring tests show that plants remain at their support cell when water is added, drained, or flowed through the cell.
- Water-aware player, fauna, and spawn tests cover water above air and water inside plant cells.

### Likely files

| Area | Files |
| --- | --- |
| Flora traits and ecology | `src/world/flora_taxa.c`, `src/world/flora_types.h`, `src/ecology/ecology_flora.c` |
| Fauna and environment queries | `src/ecology/entity_simulation.c`, `src/ecology/entity_spawn.c`, `src/app/game_environment.c` |
| Player liquid queries | `src/gameplay/player.c` |
| Focused tests | `tests/test_flora_taxa.c`, `tests/test_entity_ecology.c`, `tests/test_world_environment.c`, plus the player coverage owner selected during implementation |

### Commit boundary

Commit the complete Stage 3 behavior, tests, and runtime diagnostics as one reviewable change.

## Stage 4: deterministic generation order

**Execution status:** planned as M4.4. The behavior and acceptance rules below
remain the detailed specification.

### Goal

Make plant placement consume an already completed waterline and cell state, with a single deterministic order for every terrain generator.

### Work

- Establish this generation order: geological substrate and caves; basins, oceans, and lakes with water volume; shoreline-dependent plants using the established waterline; substrate-rooted plants and decorations.
- Compute terrain elevation, bathymetry, and effective waterline before any flora pass; expose one query for shoreline and submersion decisions instead of rediscovering water by block type.
- Make each pass write block identity and water volume independently so later planting cannot erase water and later water placement cannot silently replace a rooted plant.
- Normalize legacy generated water cells at the generation boundary so downstream passes only consume canonical `WorldCell` state.
- Keep pass order, hash inputs, and tie-breaking independent of chunk load order so seams and worker scheduling cannot change the result.

### Acceptance

- Terrain fixtures verify that no later pass overwrites generated water unless an explicit generation rule requests it.
- Shoreline plants appear at the expected waterline, wetland plants appear in the intended saturation band, and aquatic plants remain submerged where their traits require it.
- Solid cells never contain water, while air and plant cells preserve their generated volume.
- Repeating a fixed seed, generator version, and coordinates produces byte-identical block and water-volume results across single-worker and multi-worker runs.

### Likely files

| Area | Files |
| --- | --- |
| Terrain and material passes | `src/world/terrain.c`, `src/world/terrain_home_materials.c`, `src/world/block_catalog.h` |
| Flora placement and traits | `src/world/flora_taxa.c`, `src/ecology/ecology_flora.c` |
| Focused tests | `tests/test_terrain_scale.c`, `tests/test_terrain_cube.c`, `tests/test_homeworld_map_model.c`, `tests/test_flora_taxa.c` |

### Commit boundary

Commit the ordered generation pipeline and its determinism fixtures as one reviewable change.

## Stage 5: persistence and DSL

**Execution status:** complete in M3. This section remains the acceptance
specification that M3 implemented.

### Goal

Store and inspect one authoritative cell record containing both block identity and water volume, without reconstructing one field from the other.

### Work

- Replace block-only edit payloads with a canonical cell edit that carries block type and water volume through placement, undo, redo, history, and save/load.
- Establish one serialized owner for edited water state; remove duplicate fluid-edit persistence where the same cell would otherwise be written twice.
- Bump the surface save schema and reject malformed cells: unknown blocks, water on solid blocks, and out-of-range volume values must fail validation.
- Preserve full water above air and partial or full water inside plant cells across save, load, chunk unload/reload, and undo/redo.
- Add DSL fields or commands for block type, water volume, submerged state, and flora flood tolerance, including deterministic inspection at a requested coordinate.
- Extend the project DSL implementation and update `docs/debug-dsl.md` with the new runtime names and a focused smoke command.

### Acceptance

- Save/load round trips preserve block plus water volume for air, plants, partial cells, and cells at section boundaries.
- Undo and redo restore both fields atomically and never create duplicate block plus fluid edits for one coordinate.
- Fluid simulation after load observes the same volumes as before save, with no copied or lost water.
- Corrupt saves with invalid volume or solid-water combinations fail with a specific load error.
- DSL smoke tests resolve every new field or command and reproduce a flooded plant, drained plant, and water-above-air cell.

### Likely files

| Area | Files |
| --- | --- |
| Edit and world state | `src/world/block_edit_types.h`, `src/world/block_edits.h`, `src/world/world.c`, `src/world/fluid.c` |
| Save format and application | `src/world/surface_save.h`, `src/world/surface_save.c`, `src/app/game_save.c` |
| DSL implementation and documentation | `src/app/game_debug*.c`, `docs/debug-dsl.md` |
| Focused tests | `tests/test_surface_save.c`, `tests/test_save_io.c`, `tests/test_debug_dsl.c`, `tests/test_world_ecology_hooks.c` |

### Commit boundary

Commit the unified cell persistence and DSL changes as one reviewable change, followed by documentation verification.

## Whole-plan completion

The follow-up is complete when flooding changes plant survival according to traits and generated water and vegetation follow the fixed pass order. Unified cell persistence is already complete in M3. Run focused tests after M4.3 and M4.4, then the full test and documentation gates before closing M4.
