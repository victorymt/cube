# Voxelcraft world expansion roadmap

This directory is the implementation and acceptance record for the long-term
world-expansion work. A stage is planned here before implementation, updated as
work progresses, verified, and then committed as one reviewable change.
Execution order and acceptance gates are owned by the [Homeworld cube migration
execution plan](../docs/cube-migration-execution-plan.md).

## Current work

Current state: see [STATUS.md](../STATUS.md).

Dated snapshots and plans, kept for their planning evidence, not for current state: [progress report 2026-10-20](../docs/project-progress-2026-10-20.md), [historical cube activation plan](../docs/cube-activation-plan.md), [coupling review](../docs/architecture/21-coupling-review-2026-10-24.md).

## Scope inventory

Which areas have implementation behind them, and where each is recorded. This is an index into the records, not current state; what is open today lives in [STATUS.md](../STATUS.md).

| Scope | Status | Record |
| --- | --- | --- |
| Versioned Homeworld drainage, lakes, deposits and river terraces | Implemented; new worlds use terrain v11 (v8 terraces + v11 historical volcanic layers), v7/v6 retain their generation paths | [Landform design and boundaries](../docs/geographic-landforms-design.md) |
| Per-planet generation strategies and surface-isolated landform runtime | Implemented; rivers, lakes, water and sediment feed planet terrain | [Surface-neutral landforms](../docs/landform-planets.md) |
| Numerical landform quality gates | Passed in the 2026-10-07 fixtures; not a global visual guarantee | [Quality review](../docs/landform-quality.md) |
| Polar ground coverage | Open — see [STATUS.md](../STATUS.md) | [Cube contract capacity table](../docs/cube-contract.md) |
| Cube-grid coordinates, immutable v9 base generation, chunk keys, shared geometry and physical coverage | Implemented and tested; the default runtime still uses the legacy grid | [Cube-grid contract and next steps](../docs/surface-cube.md) |
| Cube-grid worker queue adapter | Implemented: retained immutable inputs, cube column/section jobs, stale-result rejection and cleanup | [Runtime stage 1](../docs/surface-cube.md#runtime-stage-1-generation-queue-adapter-2026-10-14) |
| Cube-grid edits and column admission | Implemented: face/body-addressed in-memory edits, completion replay, column/section requests and explicit release | [Runtime stage 2](../docs/surface-cube.md#runtime-stage-2-cube-edits-and-column-admission-2026-10-14) |
| Cube-grid physical admission and mesh projection | Implemented: budgeted spherical-cap admission, exact cross-face mesh halos, projected meshes and render bounds | [Runtime stage 3](../docs/surface-cube.md#runtime-stage-3-physical-admission-and-mesh-projection-2026-10-14) |
| Cube runtime entry and player loop (2026-10-22/23) | Implemented behind `surface debug cube`: gate + policy install, coverage-driven streaming, landing, walking, cube picking, cube edit save section | [Activation plan execution log](../docs/cube-activation-plan.md) |
| Cube mesh vertical stretch | Open — see [STATUS.md](../STATUS.md) | [Audit findings](../docs/cube-audit-2026-10-22.md) |
| Fluid on cube surfaces | Open — see [STATUS.md](../STATUS.md) | [Activation plan execution log](../docs/cube-activation-plan.md) |
| Topology descriptors and V29 save boundary | Implemented: explicit legacy-map/1 policy, no old-format loading; cube descriptors are admitted only while the cube runtime gate is open | [Topology contract](../docs/surface-cube.md#topology-and-save-contract-v29) |
| Chinese classical courtyard complex (2026-10-20) | Implemented: grammar v7, 73x81 south-facing walled compound with gatehouse, forecourt, side halls, raised main hall, roofs, bracket bands and courtyard details; preview E2E passes | [CGA voxel grammars](../docs/architecture/19-cga-voxel-grammar.md) |
| Coriolis terrain bias | Disabled; requires polar visual acceptance and calibrated diagnostics first | [Physics boundary](../docs/landform-planets.md) |

World saves now write and read V29 only; V28 and older require a new world. Regional
ecology payload V7 and terrain generator v11 have independent version sequences.
The authoritative version/identity/ownership/capacity tables live in the
[cube activation contract](../docs/cube-contract.md).
See [persistence boundaries](../docs/architecture/18-volcano-preview.md#state-and-persistence).

## Completed stage records

These statuses describe dated, bounded implementation work, not current visual
acceptance. In particular, the original topology and polar protocol tests do not
close the polar coverage defect above.

| Stage | Scope | Acceptance record | Status |
| --- | --- | --- | --- |
| 01 | Local climate and weather foundation | [01](01-climate-weather-foundation.md) | Complete |
| 02 | WMO cloud taxonomy and layered cloud fields | [02](02-cloud-taxonomy.md) | Complete |
| 03 | Tornado lifecycle, forces, debris, and presentation | [03](03-tornadoes.md) | Complete |
| 04 | Persistent wildfire, smoke, suppression, and recovery hooks | [04](04-wildfire.md) | Complete |
| 05 | Geological and biological block expansion | [05](05-block-expansion.md) | Complete |
| 06 | Tree and plant diversity with biome ecology | [06](06-tree-plant-diversity.md) | Complete |
| 07 | Global planetary topology and seamless traversal | [07](07-global-planetary-topology.md) | Complete |
| 08 | Cross-system integration, migration, and performance audit | [08](08-cross-system-integration.md) | Complete |
| 09 | Renderer, mesh, and simulation performance optimization | [09](09-performance-optimization.md) | Complete |
| 10 | Unified simulation clock and fixed-step scheduling | [architecture 00](../docs/architecture/00-simulation-clock.md) | Complete |
| 11 | Solar geometry, radiation balance, and conserved atmosphere | [architecture 01](../docs/architecture/01-radiation-balance.md) | Complete |
| 12 | Hydrology, groundwater, rivers, sediment, and erosion | [architecture 02](../docs/architecture/02-hydrology-model.md) | Complete |
| 13 | Multi-scale ecology, nutrient cycles, and off-screen populations | [architecture 03](../docs/architecture/03-ecology-nutrients.md) | Complete |
| 14 | Physics-driven wildfire, smoke, and supercell tornadoes | [architecture 04](../docs/architecture/04-hazard-physics.md) | Complete |
| 15 | Character, structure, and optional survival/social physics | [architecture 05](../docs/architecture/05-character-structure-physics.md) | Complete |
| 16 | Deterministic replay, graphics E2E, and performance gates | [architecture 06](../docs/architecture/06-deterministic-replay-and-ci.md) | Complete |

## Follow-on verification

Completed stages apply to the bounded scope and dated evidence in their records.
They do not certify later revisions or imply a full global simulation.

| Priority | Scope | Status and evidence |
| --- | --- | --- |
| P1 | Recheck polar graphics timeout and make E2E reply attribution reliable | Complete; [full graphics and independent 600-frame polar routes passed, 2026-09-19](../docs/architecture/15-polar-e2e-reliability.md); historical timeout not reproduced, stdin reply ordering corrected |
| P2 | Cross-system per-tick replay verification | Complete within the documented fixture/checkpoint scope; [92 regular and 92 sanitizer tests passed, 2026-09-19](../docs/architecture/14-cross-system-replay.md) |

## Future water/plant cell work

The following work continues the water/plant cell model after the volume-based cell refactor. These local stage numbers are distinct from the long-term Stage 03 tornado record above. Current state is maintained in [STATUS.md](../STATUS.md).

| Water/plant stage | Scope | Plan |
| --- | --- | --- |
| 3 | Plant/water ecological coupling, flood tolerance, damage, and anchoring | [Water/plant stages 3–5](10-water-plant-cell-followups.md#stage-3-plantwater-ecological-coupling) |
| 4 | Deterministic terrain, water, shoreline-plant, and rooted-plant generation order | [Water/plant stages 3–5](10-water-plant-cell-followups.md#stage-4-deterministic-generation-order) |
| 5 | Unified cell persistence, save validation, and DSL inspection | [Water/plant stages 3–5](10-water-plant-cell-followups.md#stage-5-persistence-and-dsl) |

## Shared acceptance rules

- Add deterministic unit coverage for simulation rules and save migrations.
- Add DSL or `--debug-*` controls for runtime-only reproduction and inspection.
- Reproduce visual behavior in the running Hyprland build where applicable.
- Keep normal gameplay free from test-only behavior.
- Validate the stage with the narrow tests first, then the repository gates
  proportionate to the affected systems.
- Update the stage document with exact evidence and known limits before commit.
- Commit every completed stage separately; do not include `AGENTS.md`.
