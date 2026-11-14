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
| 18 | [volcano, lava and ash](18-volcano-preview.md) | G1/G2 lifecycle and lava, G3 water contact, P0 ash/material products, fractional collection; retained payloads in V30 |
| 19 | [CGA voxel grammars](19-cga-voxel-grammar.md) | deterministic planning, four structure grammars, conflict resolution, cross-chunk IR evaluation, and CGA E2E coverage |
| 20 | [architecture health review (2026-10-23)](20-health-review-2026-10-23.md) | measured dependency matrix, coupling hotspots and header-width metrics; four findings, none blocking, with a suggested order |
| 21 | [coupling review (2026-10-24)](21-coupling-review-2026-10-24.md) | re-measured matrix after the module moves, surviving value-type cycles, and the measured rebuild radius of the four `world` headers six components read |
| 22 | [world.h responsibility split (2026-10-24)](22-world-header-split-2026-10-24.md) | `world.h` narrowed to chunk notifications; block types, block edits, lighting and world state are separate headers with measured rebuild radius, plus two mutation-verified include rules |
| 23 | [documentation truth gate (2026-10-24)](23-doc-truth-gate-2026-10-24.md) | why a green prose gate coexisted with three false documents, the `docs/doc-claims.json` registry, stale detection by commit comparison, and what the runtime probe costs |
| 24 | [world type decoupling (2026-10-25)](24-world-type-decoupling-2026-10-25.md) | `world_types.h` and `surface_topology.h` split into focused type headers with measured rebuild radius; two targets missed and one phase abandoned after measurement, all recorded |
| 25 | [test honesty pass (2026-10-25)](25-test-honesty-2026-10-25.md) | CI was red while local runs were green, one gate guarded the previous split, a documented invariant was a literal, and six tests existed that nothing invoked |
| 26 | [player/world-topology decoupling analysis (2026-11-12)](26-player-world-topology-decoupling-analysis-2026-11-12.md) | static dependency audit of Player movement/input/sensors/camera, cube-versus-legacy semantic differences, feasibility options and a staged migration plan; its P0 type/input split, P1 `PlayerWorldOps` motion/sensor split and P2 neutral `ActorBody` boundary are implemented in follow-ups |
| 27 | [player camera coupling analysis (2026-11-13)](27-player-camera-coupling-analysis-2026-11-13.md) | camera data flow, interaction/render/environment ordering, legacy map-cell collision and occlusion paths, FOV/render-distance coupling, and a staged camera boundary proposal |
| 28 | [camera refactor implementation (2026-11-13)](28-camera-refactor-implementation-2026-11-13.md) | neutral target views, independent camera phase, legacy/cube world-query adapter, render/streaming distance split, multi-rig routing, and entity bindings with spawn-safe handles |
| 29 | [player/world simulation target architecture (2026-11-13)](29-player-world-simulation-target-2026-11-13.md) | target boundaries for player intent, world-side motion, state ownership, fixed-step simulation, environmental fields, planet rules, reference frames, and deferred ECS evaluation |
| 30 | [player world motion P3 implementation (2026-11-13)](30-player-world-motion-p3-2026-11-13.md) | neutral world-side actor motion solver, PlayerWorldOps ActorBody boundary, compatibility facade, and behavior-preserving collision migration |
| 31 | [player fixed-step locomotion P4a (2026-11-13)](31-player-fixed-step-locomotion-p4a-2026-11-13.md) | render-frame input staging, one-shot edge consumption, fixed-step player/ship motion, camera/streaming ordering, and tornado integration |
| 32 | [player state commit P4b (2026-11-13)](32-player-state-commit-p4b-2026-11-13.md) | explicit halt/look/floating/place/restore commits, removal of app Player field writes, transition migration, and architecture enforcement |
| 33 | [world environment query P5 (2026-11-13)](33-environment-query-p5-2026-11-13.md) | world-side water/gravity samples, explicit simulation time, Player adapter aliases, and environment boundary enforcement |
| 34 | [world rules and frame foundation P6 (2026-11-13)](34-world-rules-frame-p6-2026-11-13.md) | deterministic scope/priority rule registry, add/modify/replace semantics, explicit frame identity, and supported surface-local/body transforms |
| 35 | [world rule environment consumption P6b (2026-11-13)](35-world-rule-environment-p6b-2026-11-13.md) | gravity acceleration samples, body/region rule matching, rule counts, and environment-query integration |
| 36 | [world frame cross-reference P6c (2026-11-13)](36-world-frame-cross-p6c-2026-11-13.md) | system points, system/surface/body conversion, camera-relative positions, and save-local frame marking |
| 37 | [player/entity ECS evaluation P7 (2026-11-13)](37-player-entity-ecs-evaluation-p7-2026-11-13.md) | player/entity storage and system audit, ECS decision gates, deterministic migration constraints, and the decision to defer component storage |
| 38 | [player runtime rule integration P8a (2026-11-14)](38-player-runtime-rule-integration-p8a-2026-11-14.md) | runtime WorldRuleSet ownership, combined environment samples, active-body scope, and gravity/water rules driving real Player motion |
| 39 | [frame save boundary P8b (2026-11-14)](39-frame-save-boundary-p8b-2026-11-14.md) | explicit save-local frame conversion, save/load round trip, and the topology-native renderer coordinate contract |
| 40 | [intent-driven ground locomotion P8.1 (2026-11-14)](40-intent-driven-ground-locomotion-p8-1-2026-11-14.md) | actor-neutral ground intent/profile, ordinary walk/sprint/jump simulation, and one-tick Player state commit |
| 41 | [unified player environment motion P8.2 (2026-11-14)](41-unified-player-environment-motion-p8-2-2026-11-14.md) | one intent/mode/profile pipeline for ground, water, space and floating, common collision, and a single Player body commit |
| 42 | [tornado environment sample P8.3 (2026-11-14)](42-tornado-environment-sample-p8-3-2026-11-14.md) | tornado force fields in combined environment samples, chest-offset sampling, and app external-force consumption |

Programmatic-generation review: [2026-11-12 procedural generation analysis](../procedural-generation-analysis-2026-11-12.md) records the current generation pipeline, determinism model, content gaps and suggested order.

Original acceptance narratives remain in `todo/01` through `todo/09`. The
roadmap in `todo/00-roadmap.md` also lists the physical follow-on stages 10-16,
which are represented by the records above.

[Volcanic activity design](../volcanic-activity-design.md) defines the staged
eruption, lava and ecology boundaries; G1/G2, G3 water contact and P0 ash/material
products are implemented. Player heat exposure and an accumulating damage metric are
wired into the production fixed step; full player health/death, animal heat hazards,
dynamic lava ignition and cross-process persistence remain deferred. Ecological
restoration remains deferred.

Terrain work is recorded in [surface-neutral landforms](../landform-planets.md),
[landform quality](../landform-quality.md), and the [cube-grid activation plan](../cube-activation-plan.md).
What is open today is recorded once, in [STATUS.md](../../STATUS.md).

The records below are dated evidence: each one describes what a stage changed
and what it measured. The boundaries that must not silently change are stated
once in the [architecture overview](../architecture-overview.md), and the
reasoning behind the choices that are expensive to reverse is in the
[ADR set](../decisions/README.md).

## Implementation, verification, and limits

The table identifies implemented boundaries and their commit evidence. A
roadmap status of `Complete` applies to that stage's recorded scope; it does
not imply that every later revision passed every acceptance route or that
the bounded models implement full global simulation.

- **Persistence:** the current world reader/writer accepts V30 only, with
  explicit Homeworld/planetary topology descriptors. V29 and older are rejected
  without migration. See the [V30 contract](../surface-cube.md#topology-and-save-contract-v30).
  Historical payload introductions: Radiation was introduced in V21, hydrology in V22, the
  homeworld daylight anchor in V23, volcano previews in V24, and lava physics
  in V25; later versions add water reactions, ash/material products and
  fractional collection. Regional ecology independently writes
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
  128x128 canonical region. Inter-region heat transport, dynamic global river
  transport, ocean transport, snowpack, and off-screen catchment integration
  remain outside those runtime boundaries. Static landform generation uses a
  separate global drainage atlas. Regional evolution keeps at
  most eight lineage slots per region and bounded population caches.
