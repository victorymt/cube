# Homeworld cube-grid migration execution plan

- Status: active execution plan
- Baseline: `aa67764`
- Scope: Homeworld only. Planets remain on `legacy-map/1` for this plan.
- Current truth: [STATUS.md](../STATUS.md)
- Version, identity and capacity contract: [Cube-grid activation contract](cube-contract.md)
- Historical plan: [Cube-grid activation plan](cube-activation-plan.md)

This document owns execution order and exit criteria. It does not replace the
current-state entries in `STATUS.md` or the frozen tables in
`docs/cube-contract.md`. Every completed stage updates `STATUS.md`, records its
evidence here, and lands as its own commit.

## 1. Starting point

The repository has two live surface representations:

- `legacy-map/1` remains the default surface backend; V30 now stores topology per surface.
- `cube-grid/1` is reachable through the DSL command `surface debug cube`.

The cube runtime path already supports landing, walking, picking, edits,
physical coverage streaming, generation and rendering behind that debug entry.
V30 also restores a saved cube topology in a fresh process without that debug
entry. The migration is not complete:

- fluid refuses to run on cube surfaces because its queue keys do not include
  the face identity;
- most ecology, generation, CGA, hydrology and sampling consumers still consume
  map-shaped coordinates or map chunk identity.

Execution state:

| Stage | State | Result |
| --- | --- | --- |
| M0 | complete | The Homeworld-only execution plan and its links are established. |
| M1 | complete | Legacy and cube share the topology-neutral `SurfaceContract`. |
| M2 | complete | Cube mesh vertices, transforms and bounds share the section anchor frame. |
| M3 | complete | V30 persists topology, canonical pose and complete cell edits. |
| M4 | active | M4.0 is complete; migrate the remaining Homeworld consumers, then switch its default. |
| M5 | pending | Close physical coverage, visual and performance acceptance. |
| M6 | pending | Remove Homeworld-only transition paths and close the migration. |

This plan deliberately does not migrate planets. The surface interfaces and
V30 records are body-addressed so a later planet plan can reuse them, but no
planet cube acceptance belongs to G1-G5 here.

## 2. Architectural rule

Consumer unification means more than hiding a backend enum. Consumers must use
semantic surface operations and values that do not expose either topology:

- stable identity is `SurfaceAddress`;
- a cell is `WorldCell`, containing block identity and authoritative water
  volume;
- continuous player state is `SurfacePose`;
- chunk and section access uses an opaque surface chunk handle;
- terrain, waterline, bathymetry and biome sampling use surface cells;
- adjacency, regions, coverage and admission are semantic operations.

The world backend, application composition root, save/load code and explicit
debug diagnostics may know which topology is active. Ecology, gameplay,
presentation and ordinary application logic may not. In particular, these
modules must not include cube chunk keys, call `WorldCube*`, call map chunk
lookup helpers, or branch on `SURFACE_TOPOLOGY_CUBE_GRID` to perform gameplay
work.

The contract is implemented as several focused operation tables rather than one
large `SurfaceWorld` interface. Legacy and cube adapters implement the same
contract. The active backend is selected once when a surface is installed.

## 3. Acceptance gates

### G1: cube core visual loop

- `surface debug cube` reaches a stable Homeworld cube surface on Hyprland.
- The player lands, walks, picks and edits on both sides of a face edge.
- A same-process unload/reload preserves cube edits.
- DSL screenshots show neither the vertical stretch nor the green wall.
- Fluid is not part of G1.

### G2: Homeworld V30

- V30 serializes per-surface topology, player `SurfacePose`, and edits as
  `SurfaceAddress + WorldCell` records.
- Block identity, water volume, undo and redo survive save/load.
- A fresh process loads a V30 Homeworld without `surface debug cube`.
- V29 and older files are rejected with a specific error and are not deleted or
  modified. Invalid, truncated and solid-water records fail transactionally.

### G3: Homeworld consumer parity

- Fluid, water/plant ecology and deterministic generation, entity/ecology,
  CGA, volcano/lava/ash, hydrology/radiation and terrain sampling consume the
  surface contract on Homeworld.
- No production consumer performs a topology branch for Homeworld behavior.
- A new Homeworld defaults to cube-grid while planets continue to work through
  the legacy adapter.
- Cross-face water, flora and structures pass deterministic tests.

### G4: coverage and performance

- Scheduling, admission, retention, LOD and drawing use one physical coverage
  metric.
- The fixed 625-slot capacity remains the default. Saturation is explicit and
  uses the current nearest-first policy; this plan does not add unbounded
  capacity or a new layered LOD scheme.
- Six faces, face edges, face corners, poles, high altitude and multiple render
  distances pass the Homeworld graphics and performance gates.
- The final evidence records frame time, memory, requested/admitted/ready and
  saturation data from the same machine and scene.

### G5: Homeworld migration close

- Dead Homeworld-only activation paths and map aliases are removed. The legacy
  backend remains for planets.
- Architecture tests reject topology or legacy chunk identity in consumer
  modules.
- Normal, sanitizer, header, DSL, documentation, cross-process save and
  Hyprland graphics gates pass.

## 4. Stages

### M0: establish the execution baseline

**Status:** complete (2026-11-09)

Work:

- add this execution document;
- link it from the current-state and roadmap documents;
- mark superseded plans as historical;
- record the Homeworld-only scope without changing the long-term contract.

Exit evidence: `make check-docs` passed on 2026-11-09 with 73 documents,
0 errors and 0 warnings. Commit the complete M0 change.

### M1: freeze the topology-neutral surface contract

**Status:** complete (2026-11-09)

Work:

- define cell read/write/known, terrain/water/biome sample, adjacency, opaque
  chunk/section, region and coverage operation tables;
- adapt both legacy-map and cube-grid behind those tables;
- allow an independent topology per surface, with Homeworld cube and planets
  legacy for this migration;
- add contract tests and an architecture allowlist for the few topology-aware
  files.

Public interface changes:

- `SurfaceAddress`, `WorldCell` and `SurfacePose` become the consumer-facing
  surface values;
- opaque chunk handles replace public `SurfaceChunkKey` and
  `SurfaceCubeChunkKey` use by consumers;
- no consumer-facing API accepts map `(x,z)` as a generic surface identity.

Exit evidence: both adapters pass `test_surface_contract`; the legacy and cube
tests cover identity, known/read/write cells, opaque chunk/section handles,
horizontal and cross-face adjacency, sampling, region identity, physical
coverage and transactional installation. `test_world_surface_snapshot`,
`test_save_format`, `test_surface_coverage`, `test_world_environment`,
`test_player_collision`, `test_world_block_box` and `test_world_block_landing`
also pass. `make test-headers` compiled 222 public headers, and
`make test-architecture` rejects any new backend token outside the exact
`tests/surface_contract_boundary_allowlist.txt` debt list. M1 uses the
topology-neutral contract as the sole owner of the active cube context. Commit
M1.

### M2: close G1 core cube loop

**Status:** complete (2026-11-09)

Work:

- reproduce the green wall with a deterministic DSL scene and screenshots;
- trace shared projected geometry, section/chunk transforms and model bounds;
- fix the remaining mesh transform issue without introducing a second vertex
  convention;
- add the regression scene and update `docs/debug-dsl.md` if a diagnostic is
  missing.

Exit evidence: `test_cube_generation` now checks every transformed mesh vertex
against the section anchor frame, verifies section bounds contain those
vertices, checks synthetic and real varied-terrain slab extents, and checks
coverage follows a distant face-edge focus. The Hyprland replay
`make test-cube-vertical-geometry-e2e` captures the same flat 5x5 stone wall
through legacy and cube, with both screenshots at camera
`0.000000,10.550000,0.000000`; the cube capture keeps the wall vertical and
aligned instead of stretching it. Artifacts are retained under
`/tmp/voxelcraft-cube-vertical.*`. The green background columns visible near
the original spawn are shared by legacy and cube and are not evidence for this
cube-only transform bug. The screenshot route claims vertical geometry and
bounds, not a live cross-face walk; adjacency, edits and distant coverage remain
covered by `test_cube_generation`. The historical exclusion trail is retained in
[the 2026-10-22 audit](cube-audit-2026-10-22.md). Commit M2.

### M3: close G2 with V30

**Status:** complete (2026-11-09)

Work:

- bump the world save format and surface save schema to V30;
- serialize topology per surface and store edits as complete `WorldCell`
  records;
- serialize canonical player `SurfacePose` state;
- make undo/redo and fluid displacement restore block and water volume
  atomically;
- make startup install the cube backend from a V30 topology record;
- reject V29 and older files without migration or deletion;
- add DSL fields/commands for block type, water volume, submerged state and
  plant flood tolerance as required by the water/plant Stage 5 specification.

This stage absorbs water/plant Stage 5. The detailed behavior remains backed by
`todo/10-water-plant-cell-followups.md`; its ordering and status are governed by
this document.

Exit evidence: `test_save_format`, `test_cube_address_save`,
`test_game_save_spherical`, `make test-cube-save-reload`,
`make test-cube-v30-reload-e2e`, `make test-v30-cell-reload-e2e` and
`make test-surface-topology-e2e` pass. V30 uses `TOPOL002`, canonical
`SurfacePose`, and a `CUBADDR2` `WorldBlockAddress + WorldCell` edit block.
Fresh processes restore cube topology without the debug gate. V29 and older are
rejected without modification, late corrupt payloads restore the checkpoint,
and volume-aware undo/redo plus plant/air water cells survive a fresh load.
Commit M3.

### M4: close G3 consumer migration

Each substage is independently reviewable, verified and committed. A substage
starts by reproducing its actual failure with the project DSL or `--debug-*`
diagnostics; missing diagnostics are added through the DSL and documented.

#### M4.0: shared consumer fixture

**Status:** complete (2026-11-09)

Build one contract-driven fixture whose assertions run unchanged against both
legacy and cube surfaces. It must cover known/read/write cells, terrain and water
samples, opaque chunk identity, horizontal and cross-face neighbours, and an
unload/reload cycle with two cube faces that share the same U/V. This stage does
not change production behavior.

Exit: both adapters pass the same fixture; cube capability-gated operations are
tested without introducing a topology branch into the fixture. Commit
`test(surface): add shared consumer contract fixture`.

Exit evidence: `tests/surface_contract_fixture.c` now owns the shared
known/read/write, sample, opaque chunk/section, bidirectional neighbour,
cross-face, distinct-chunk and reload assertions. Both adapters pass it through
`./build/normal/tests/test_surface_contract`; the cube case installs two faces
with the same U/V and verifies that their chunk identities remain distinct.
`make test-surface-cube` passes all eight focused binaries. No production code
changed.

#### M4.1: fluid chunk identity

**Status:** complete (2026-11-09)

Replace the fluid queue, edit index, chunk bucket and checkpoint authority with
`SurfaceAddress` for cells and opaque `SurfaceChunkHandle` for chunks. The legacy
`(surfaceId, chunkX, chunkZ)` pair is no longer an identity. Keep lookup and
state mutation in `fluid.c` under `SurfaceContract`; any runtime chunk access is
an internal world contract operation, never a cube-key decode in fluid code.
Cube fluid remains disabled at the end of this stage.

Exit: two cube faces with the same U/V never share a bucket; legacy fluid tests
remain green; fluid checkpoints and section unload/reload preserve face
identity through the codec fixture. Commit
`refactor(fluid): key queues by surface chunk handle`.

Exit evidence: fluid queue cells and edit records carry canonical
`SurfaceAddress` identity, and queue buckets are keyed only by opaque
`SurfaceChunkHandle`. FLD3 persists the address, chunk handle and legacy
coordinate alias, while FLD1/FLD2 remain readable. `test_fluid` proves two cube
faces with equal U/V have different chunk identities, and its FLD3 checkpoint
round trip is byte-identical. `make test-cube-save-reload` and
`make test-v30-cell-reload-e2e` pass, and all legacy fluid cases remain green.
Cube simulation is still disabled pending M4.2. Commit
`refactor(fluid): key queues by surface chunk handle`.

#### M4.2: enable cube fluid

**Status:** complete (2026-11-09)

Remove the cube early return and route cell lookup, vertical and horizontal
neighbours, dirty marking and saved-vs-implicit section handling through the
surface contract. Add deterministic cross-face flow and conservation tests plus
a DSL scene that fills, drains and reloads water across a face edge. Saving,
loading and section unload/reload must not copy water to another face.

Exit evidence: `test_fluid_cube` installs a real cube contract and two loaded
cube chunks across a face edge, carries finite water across the edge and checks
total conservation. The Hyprland replay `make test-cube-fluid-face-edge-e2e`
repeats the scenario through the DSL. `surface pose LONGITUDE LATITUDE RADIAL
YAW PITCH` and the `fluid.inspect_*` fields were added to make geographic edge
placement and targeted fluid inspection deterministic. Cube cell lookup,
neighbour stepping, dirty marking and chunk reload now route through the
surface contract. Commit `feat(fluid): run volume simulation across cube faces`.

Exit: cube and legacy fluid suites pass the same conservation cases; the DSL
scene reproduces the edge flow; `make test-cube-save-reload` passes. Commit
`feat(fluid): run volume simulation across cube faces`.

#### M4.3: water/plant Stage 3

**Status:** complete (2026-11-09)

Implement the Stage 3 specification in
`todo/10-water-plant-cell-followups.md`: deterministic flood tolerance, bounded
exposure and recovery, plant anchoring, and volume-based water queries in
player, fauna, flora and spawn paths. Do not derive occupancy from
`GetBlockAt(...) == BLOCK_WATER`.

Exit: dry, saturated, briefly submerged and chronically submerged fixtures pass
for every tolerance class; drainage and flow do not dislodge anchored plants;
dead plants are removed exactly once without duplicating biomass or structures.
Commit
`feat(ecology): couple plant survival to cell water volume`.

Exit evidence: flora taxa now classify intolerant, tolerant and aquatic plants;
`FloraFloodAdvance` provides bounded deterministic exposure, damage and dry
recovery. `ChunksAdvanceFloraFlood` runs on each simulation step, removes only
the dead structure's owned blocks and preserves the water volume in the shared
cell. `test_flora_taxa` and `test_flora_flood` pass the tolerance, recovery,
removal and water-preservation cases. Water sampling in player, fauna and spawn
paths now uses cell volume rather than block identity. Commit M4.3.

#### M4.4: water/plant Stage 4

**Status:** complete (2026-11-09)

Implement the Stage 4 generation order: substrate and caves, basins/oceans/lakes
with volume, shoreline plants from the established waterline, then rooted plants
and decorations. Writes must preserve the independent block and water fields,
and pass order, hashes and tie-breaking must not depend on worker count or chunk
arrival order.

Exit: fixed seeds produce identical block and water-volume output with one and
multiple workers; generated water is not erased by later passes; shoreline,
wetland and aquatic placement satisfy their traits. Commit
`feat(terrain): order water and flora generation deterministically`.

Exit evidence: planet generation now samples columns first, then runs separate
substrate, liquid, shoreline-cover and ecology-decoration passes; Home
generation places shoreline cover before general ground cover and rooted trees.
`test_terrain_scale` compares repeated full-cell hashes and flora structures,
and checks that generated water cells remain valid when they carry plants. The
intended shoreline output changed the pinned temperate planet baseline; the
other planet baselines and all existing landform checks remain unchanged.
Commit M4.4.

#### M4.5: entities and ecology

**Status:** complete (2026-11-09)

Move entity ground queries, spawn placement, flora updates and regional ecology
inputs from map chunk identity to surface cell/sample/region operations. Keep
off-screen state deterministic and avoid treating an unresolved cell as air.

Exit: shared legacy/cube fixtures cover ground, blocking, spawning and flora at
face centres and edges; no ecology consumer names a backend chunk key or
topology enum. Commit `refactor(ecology): consume surface cells and samples`.

Exit evidence: `WorldSurfaceCellAt`, `WorldSurfaceCellKnownAt` and
`WorldSurfaceBiomeAt` expose contract-backed cell and sample reads to ordinary
consumers. Entity collision, ground support, movement, aquatic spawn search,
wildfire shelter and roof checks use the cell facade; unresolved cells are
conservative and no entity consumer falls back to legacy map blocks. Home and
planet ecology height queries use `WorldSurfaceHeightAt`, with the Home biome
read through the contract. `test_surface_contract`, `test_world_environment`,
`test_entity_replay`, `test_entity_ecology` and `test_ecology_system` pass, and
the complete `make test` run reports 135 passed, 0 failed. Hyprland
`test-cube-fluid-face-edge-e2e` and `test-v30-cell-reload-e2e` also pass. Commit
M4.5.

#### M4.6: CGA

**Status:** complete (2026-11-09)

Plan and clip CGA structures in surface cells, including structures that cross a
face edge. Keep grammar versions unchanged unless grammar semantics actually
change; adapt address transport, occupancy and clipping behind the surface
contract.

Exit: deterministic house, dungeon and classical-complex fixtures produce the
same logical cells on legacy and cube; no block is duplicated or dropped at a
face edge. Commit `refactor(cga): place structures in surface cells`.

Exit evidence: CGA planning now accepts a bounded map-space planning region and
an explicit terrain sampler and world seed. The legacy chunk path supplies the
existing `TerrainHeight`/`BiomeAt` sampler, while cube column and section jobs
sample the immutable cube context at the corresponding `SurfaceAddress`.
Expanded writes are emitted through `CgaVisitSurfaceCells`, which converts each
logical map cell to the topology-neutral surface address before the cube writer
clips it to the staging chunk. `test_cube_generation` expands house, dungeon and
classical-complex fixtures across a real face transition and verifies that both
faces receive cells without duplicate or missing addresses; it also checks
seeded, bounded planning is deterministic and stays within the fixed feature
capacity. `test_cga_voxel`, `test_cube_generation`, `test_terrain_cube` and
`test_terrain_scale` pass, as do `make test-headers`, `make test-werror`,
`make test-architecture`, the Hyprland `test-cube-fluid-face-edge-e2e` and
`test-v30-cell-reload-e2e` runs, and the complete `make test` run (135 passed,
0 failed). Commit M4.6.

#### M4.7: volcano, lava and ash

Migrate volcano addresses, history search and section effects to surface
identity. Keep finite-volume lava/water displacement and persistence semantics
unchanged while removing map chunk lookup from gameplay behavior.

Exit: volcano history, mountain, overlay and water-contact suites pass on cube
and legacy; cross-face and polar search cases return canonical surface cells.
Commit `refactor(geology): address volcanic systems through surfaces`.

#### M4.8: hydrology and radiation

Bind precipitation, groundwater, runoff, radiation regions and activity to
opaque surface region handles. A region transition must use the contract's
semantic region operation rather than comparing backend chunk coordinates.

Exit: hydrology and radiation fixtures run on both adapters with equivalent
region results; cube face-edge regions do not leak across faces; no consumer
branches on topology for Homeworld behavior. Commit
`refactor(hydrology): bind activity to surface regions`.

#### M4.9: terrain sampling cleanup

Migrate the remaining elevation, waterline, bathymetry and biome callers to one
surface sampling path. Remove obsolete map-only aliases from consumer modules
and shrink `tests/surface_contract_boundary_allowlist.txt` to the exact paths
that still require app composition or explicit diagnostics.

Exit: no gameplay, ecology or presentation consumer calls map chunk lookup or a
cube chunk key; sampling fixtures agree across adapters; architecture tests use
the reduced allowlist. Commit `refactor(world): finish surface sampling migration`.

#### M4.10: default Homeworld cube switch

Change only the new-Homeworld default to cube-grid. Planets continue to select
the legacy adapter. Keep the debug activation route temporarily for explicit
cube tests, but normal Homeworld creation, save, load, streaming and gameplay
must no longer require it.

Exit: a fresh normal Homeworld starts as cube-grid; a planet round trip still
uses legacy; every M4 consumer scenario passes; production consumers have no
Homeworld topology branch. Record the final M4 evidence in this section and
`STATUS.md`. Commit `feat(world): default Homeworld to cube-grid`.

### M5: close G4

#### M5.0: freeze the coverage evidence surface

Freeze one fixture and diagnostic schema for requested, admitted, ready,
pending, missing, submitted, released and saturated values. Separate local
terrain readiness from visual readiness. Commit
`test(coverage): freeze physical coverage evidence`.

#### M5.1: unify scheduling, admission and eviction

Make scheduling, admission, retention and eviction consume one physical
coverage result while preserving the 625-slot nearest-first policy. Stale,
duplicate and saturated requests must remain explicit and atomic. Commit
`refactor(streaming): share one physical coverage result`.

#### M5.2: align LOD and drawing readiness

Make LOD selection and drawing consume the same coverage and readiness model.
Do not report requested or admitted slots as drawn coverage, and do not hide
saturation behind a larger distance or silent eviction. Commit
`refactor(render): consume physical coverage readiness`.

#### M5.3: run the six-face visual matrix

Add deterministic Hyprland scenes for all six faces, face edges, corners, both
poles, high altitude and multiple render distances. Each scene records camera,
focus, requested/admitted/ready counts, saturation and screenshot artifacts.
Commit `test(cube): cover the Homeworld visual matrix`.

#### M5.4: close coverage and performance evidence

Repeat frame-time, memory, queue and saturation measurements on the same machine
and scenes. If a budget fails, record the deficit and stop rather than changing
capacity silently. Update `STATUS.md`, record the M5 evidence here and commit the
complete G4 closure.

### M6: close G5

#### M6.0: remove the Homeworld activation gate

Remove the debug-only Homeworld gate and its switch aliases once normal creation
and V30 load both select topology directly. Keep explicit diagnostics that are
still needed to inspect the active backend. Commit
`refactor(cube): remove the Homeworld debug activation gate`.

#### M6.1: enforce consumer boundaries

Reduce the architecture allowlist to composition, save orchestration and
explicit diagnostics only. Add failing fixtures for a topology enum, cube chunk
key or map chunk lookup injected into a normal consumer. Commit
`test(architecture): enforce surface consumer boundaries`.

#### M6.2: delete Homeworld-only legacy paths

Delete dead Homeworld map activation and compatibility aliases while retaining
the legacy adapter required by the explicitly deferred planet migration. Commit
`refactor(world): remove Homeworld legacy transition paths`.

#### M6.3: finish DSL and documentation truth

Remove obsolete debug commands and fields, then update the DSL reference,
contracts, architecture overview, roadmap and `STATUS.md`. Historical records
remain historical; only current-state pointers and broken links are corrected.
Commit `docs(cube): close the Homeworld migration record`.

#### M6.4: run the final migration gate

Run the complete normal, sanitizer, header, architecture, documentation, DSL,
V30, surface-topology, polar and Hyprland graphics gates. Record every command,
exit status and artifact, and list unknown or failed checks explicitly. Update
`STATUS.md` and close G1-G5. Commit
`test(cube): verify the completed Homeworld migration`.

## 5. Deferred work

The following is not part of this plan:

- migrating planets to cube-grid;
- deleting the legacy adapter while planets still use it;
- choosing a new coverage capacity or layered-LOD architecture;
- preserving or migrating V29 worlds;
- expanding unrelated weather, ecology, building or gameplay scope.

Planet migration and final legacy removal should start from the shared surface
contract and reuse the Homeworld acceptance evidence, not fork it.

## 6. Verification and execution protocol

- Every stage starts by reproducing its current failing behavior with the
  project DSL or `--debug-*` options. Missing diagnostics are added through the
  DSL and documented in `docs/debug-dsl.md`; do not use compositor control
  commands to bypass the project's test surface.
- Every stage records HEAD, commands, exit status, focused test names and
  artifact paths in its commit or linked evidence.
- A stage is not complete until its code, tests, diagnostics, current-state
  entry and execution-plan evidence land together. Commit each numbered
  substage separately; do not combine adjacent M4-M6 tasks.
- Code stages run focused tests, `make test`, `make test-architecture` and
  `make check-docs`. DSL changes additionally run `make docs-probe`,
  `make test-debug-cli` and `make test-debug-stdin-protocol`.
- Graphics stages run the Hyprland E2E targets and store deterministic scenes,
  not only interactive observations.
- The final G5 gate runs `make test-sanitize`, `make test-headers`,
  `make test-e2e`, `make test-cube-save-reload`, the surface-topology and polar
  E2E targets, and the debug-graphics E2E target.

