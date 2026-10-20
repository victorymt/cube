# Voxelcraft world expansion roadmap

This directory is the implementation and acceptance record for the long-term
world-expansion work. A stage is planned here before implementation, updated as
work progresses, verified, and then committed as one reviewable change.

## Current work (2026-10-08)

| Scope | Status | Record |
| --- | --- | --- |
| Versioned Homeworld drainage, lakes, deposits and river terraces | Implemented; new worlds use terrain v8, v7/v6 retain their generation paths | [Landform design and boundaries](../docs/geographic-landforms-design.md) |
| Per-planet generation strategies and surface-isolated landform runtime | Implemented; rivers, lakes, water and sediment feed planet terrain | [Surface-neutral landforms](../docs/landform-planets.md) |
| Numerical landform quality gates | Passed in the 2026-10-07 fixtures; not a global visual guarantee | [Quality review](../docs/landform-quality.md) |
| Polar ground coverage | Open: both wet and dry polar scenes show a narrow strip | [Reproduction and diagnosis](../docs/landform-quality.md#visual-observations-and-acceptance-limits) |
| Cube-grid coordinates, immutable v9 base generation, chunk keys, shared geometry and physical coverage | Independently implemented and tested; production queues, gameplay, simulation and saves still pending | [Cube-grid contract and next steps](../docs/surface-cube.md) |
| Cube-grid worker queue adapter (2026-10-14) | Implemented: retained immutable inputs, cube column/section jobs, stale-result rejection and cleanup; normal runtime scheduling is still legacy | [Runtime stage 1](../docs/surface-cube.md#runtime-stage-1-generation-queue-adapter-2026-10-14) |
| Cube-grid edits and column admission (2026-10-14) | Implemented: face/body-addressed in-memory edits, completion replay, column/section requests and explicit release; player consumers, coverage policy and V30 remain pending | [Runtime stage 2](../docs/surface-cube.md#runtime-stage-2-cube-edits-and-column-admission-2026-10-14) |
| Cube-grid physical admission and mesh projection (2026-10-14) | Implemented: budgeted spherical-cap admission, exact cross-face mesh halos, projected meshes and render bounds; normal-game activation still gated on gameplay/simulation/save conversion | [Runtime stage 3](../docs/surface-cube.md#runtime-stage-3-physical-admission-and-mesh-projection-2026-10-14) |
| Topology descriptors and V29 save boundary | Implemented: explicit legacy-map/1 policy, cube-grid runtime rejection, no old-format loading | [Topology contract](../docs/surface-cube.md#topology-and-save-contract-v29) |
| Chinese classical courtyard complex (2026-10-20) | Implemented: grammar v7, 73x81 south-facing walled compound with gatehouse, forecourt, side halls, raised main hall, roofs, bracket bands and courtyard details; preview E2E passes | [CGA voxel grammars](../docs/architecture/19-cga-voxel-grammar.md) |
| Coriolis terrain bias | Disabled; requires polar visual acceptance and calibrated diagnostics first | [Physics boundary](../docs/landform-planets.md) |

Next: integrate cube-cell identity across
terrain and gameplay, unify scheduling/render coverage, then replay polar,
face-crossing, editing and save/load routes. Both Homeworld and planets will
switch to cube-grid/1 and terrain v9; preserving v7/v8 fingerprints is no longer
a requirement. V30 will reject V29 and older files without migrating or deleting
them. These are activation targets, not the current runtime defaults.

World saves now write and read V29 only; V28 and older require a new world. Regional
ecology payload V7 and terrain generator v8 have independent version sequences.
See [persistence boundaries](../docs/architecture/18-volcano-preview.md#state-and-persistence).

## Completed stage records

These statuses describe dated, bounded implementation work, not current visual
acceptance. In particular, the original topology and polar protocol tests do not
close the polar coverage defect above.

| Stage | Scope | Status |
| --- | --- | --- |
| 01 | Local climate and weather foundation | Complete |
| 02 | WMO cloud taxonomy and layered cloud fields | Complete |
| 03 | Tornado lifecycle, forces, debris, and presentation | Complete |
| 04 | Persistent wildfire, smoke, suppression, and recovery hooks | Complete |
| 05 | Geological and biological block expansion | Complete |
| 06 | Tree and plant diversity with biome ecology | Complete |
| 07 | Global planetary topology and seamless traversal | Complete |
| 08 | Cross-system integration, migration, and performance audit | Complete |
| 09 | Renderer, mesh, and simulation performance optimization | Complete |
| 10 | Unified simulation clock and fixed-step scheduling | Complete |
| 11 | Solar geometry, radiation balance, and conserved atmosphere | Complete |
| 12 | Hydrology, groundwater, rivers, sediment, and erosion | Complete |
| 13 | Multi-scale ecology, nutrient cycles, and off-screen populations | Complete |
| 14 | Physics-driven wildfire, smoke, and supercell tornadoes | Complete |
| 15 | Character, structure, and optional survival/social physics | Complete |
| 16 | Deterministic replay, graphics E2E, and performance gates | Complete |

## Follow-on verification

Completed stages apply to the bounded scope and dated evidence in their records.
They do not certify later revisions or imply a full global simulation.

| Priority | Scope | Status and evidence |
| --- | --- | --- |
| P1 | Recheck polar graphics timeout and make E2E reply attribution reliable | Complete; [full graphics and independent 600-frame polar routes passed, 2026-09-19](../docs/architecture/15-polar-e2e-reliability.md); historical timeout not reproduced, stdin reply ordering corrected |
| P2 | Cross-system per-tick replay verification | Complete within the documented fixture/checkpoint scope; [92 regular and 92 sanitizer tests passed, 2026-09-19](../docs/architecture/14-cross-system-replay.md) |

## Shared acceptance rules

- Add deterministic unit coverage for simulation rules and save migrations.
- Add DSL or `--debug-*` controls for runtime-only reproduction and inspection.
- Reproduce visual behavior in the running Hyprland build where applicable.
- Keep normal gameplay free from test-only behavior.
- Validate the stage with the narrow tests first, then the repository gates
  proportionate to the affected systems.
- Update the stage document with exact evidence and known limits before commit.
- Commit every completed stage separately; do not include `AGENTS.md`.
