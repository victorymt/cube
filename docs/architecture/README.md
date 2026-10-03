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
| 03 | [nutrient ecology](03-ecology-nutrients.md) | `cbc9057`, `8007cf6`, `855e47b`, `a90a874`, `00a285f`; conserved nutrients, directional gene flow, species branches, entity feedback, V6 population payload, and passive-spawn catalog registration |
| 04 | [hazard physics](04-hazard-physics.md) | `ee11e25`; fire/supercell tests |
| 05 | [character/structure physics](05-character-structure-physics.md) | `9991f92`; force/collapse/survival tests |
| 06 | [replay and CI](06-deterministic-replay-and-ci.md) | `672a06a`, `89d7c84`; replay, sanitizer, graphics gates |
| 07 | [global topology](07-global-topology.md) | `5b6c02b`, `3db802d`; seam/pole/save E2E |
| 08 | [cross-system identity](08-cross-system-integration.md) | `3db802d`; weather/ecology/save integration |
| 09 | [performance](09-performance-optimization.md) | `28362db`, `bc71320`, `813d0e9`; benchmark invariants |
| 10 | [radiation runtime integration](10-radiation-runtime-integration.md) | `7f6c8a7`; fixed-step climate column and V21 persistence |
| 11 | [hydrology runtime integration](11-hydrology-runtime-integration.md) | `8c127e1`; bounded water runtime, climate feedback, and V22 persistence |
| 12 | [optional plugin boundary](12-plugin-boundary.md) | `f5e3a1d`, `e44970f`, `e89e62f`; ABI v2, deterministic loader, independent shared-library targets, and versioned read-only services including hydrology |
| 13 | [component build boundaries](13-component-build-boundaries.md) | `f124765`, `f8bc443`, `ff84e3c`, `b575e4b`, `46a59f1`, `5b91c71`, `997635d`; seven independently rebuildable archives, manifest-driven targets, explicit composition root, and neutral resource, lighting, weather, ecology, and surface-state boundaries |
| 14 | [cross-system replay verification](14-cross-system-replay.md) | `test_simulation_integration`; per-tick clock, weather, ecology, voxel water and fire comparisons, with explicit checkpoint and scene limits |
| 15 | [polar E2E reliability](15-polar-e2e-reliability.md) | stdin reply-ordering regression, independent 600-frame polar DSL gate, and retained failure diagnostics |
| 16 | [regional ecology continuity](16-ecology-continuity.md) | V23 daylight anchor, V7 regional ecology persistence, explicit scheduling, and historical environmental sampling |
| 17 | [animal behavior and poses](17-fauna-poses.md) | V5 entity pose persistence, contact-driven gait/strikes, articulated behavior rendering and deterministic DSL previews |

Original acceptance narratives remain in `todo/01` through `todo/09`. The
roadmap in `todo/00-roadmap.md` also lists the physical follow-on stages 10-16,
which are represented by the records above.

Proposals, not implementation records: [volcanic activity design](../volcanic-activity-design.md)
defines the proposed local eruption, lava, persistence, and ecology boundaries.

## Implementation, verification, and limits

The table identifies implemented boundaries and their commit evidence. A
roadmap status of `Complete` applies to that stage's recorded scope; it does
not imply that every later revision passed every acceptance route or that
the bounded models implement full global simulation.

- **Persistence:** the current world writer emits V23 and reads compatible
  V18-V22 saves. Radiation was introduced in V21, hydrology in V22, and the
  homeworld daylight anchor in V23. Regional ecology independently writes
  population payload V7. These are separate version sequences.
- **Verification:** [record 06](06-deterministic-replay-and-ci.md) is the
  dated evidence and replay-coverage record. Its 2026-08-30 result of 86
  tests passing under regular and ASan/UBSan builds is historical evidence,
  not a current-HEAD certification. The bounded graphics route passed in
  that session; its full route recorded a final polar-input timeout.
  [Record 14](14-cross-system-replay.md) tracks the later P2 integration gate
  and its own dated test results. [Record 15](15-polar-e2e-reliability.md)
  records the successful 2026-09-19 full graphics recheck, reply-ordering fix
  and independent polar gate; the historical timeout was not reproduced.
- **Simulation limits:** radiation and hydrology integrate one active
  128x128 canonical region. Inter-region heat transport, a global river
  network, ocean transport, snowpack, and off-screen catchment integration
  remain outside those runtime boundaries. Regional evolution keeps at
  most eight lineage slots per region and bounded population caches.
