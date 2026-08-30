# Architecture Records

This directory is the stage-level index for the reality-simulation roadmap.
Each record names the implementation boundary, tests, commit evidence, and
known limits. The original world-expansion stages are retained alongside the
new physical-simulation stages.

| Stage | Record | Primary evidence |
| --- | --- | --- |
| 00 | [simulation clock](00-simulation-clock.md) | `d88fe09`, `e5612eb`; fixed-step and save reset tests |
| 01 | [radiation balance](01-radiation-balance.md) | `b595ecd`; Kepler, insolation, energy tests |
| 02 | [hydrology](02-hydrology-model.md) | `148bcfe`; water/sediment conservation tests |
| 03 | [nutrient ecology](03-ecology-nutrients.md) | `76f592c`; nutrient/seed/competition tests |
| 04 | [hazard physics](04-hazard-physics.md) | `ee11e25`; fire/supercell tests |
| 05 | [character/structure physics](05-character-structure-physics.md) | `9991f92`; force/collapse/survival tests |
| 06 | [replay and CI](06-deterministic-replay-and-ci.md) | `672a06a`, `89d7c84`; replay, sanitizer, graphics gates |
| 07 | [global topology](07-global-topology.md) | `5b6c02b`, `3db802d`; seam/pole/save E2E |
| 08 | [cross-system identity](08-cross-system-integration.md) | `3db802d`; weather/ecology/save integration |
| 09 | [performance](09-performance-optimization.md) | `28362db`, `bc71320`, `813d0e9`; benchmark invariants |
| 10 | [radiation runtime integration](10-radiation-runtime-integration.md) | `7f6c8a7`; fixed-step climate column and V21 persistence |
| 11 | [hydrology runtime integration](11-hydrology-runtime-integration.md) | `8c127e1`; bounded water runtime, climate feedback, and V22 persistence |
| 12 | [optional plugin boundary](12-plugin-boundary.md) | pending; stable ABI, deterministic loader, and parallel shared-library targets |
| 13 | [component build boundaries](13-component-build-boundaries.md) | current; seven independently rebuildable archives and explicit composition root |

Original acceptance narratives remain in `todo/01` through `todo/09`. The
roadmap in `todo/00-roadmap.md` also lists the physical follow-on stages 10-16,
which are represented by the records above.
