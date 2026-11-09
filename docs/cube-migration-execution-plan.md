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

- `legacy-map/1` remains the default surface backend and uses V29 world saves.
- `cube-grid/1` is reachable through the DSL command `surface debug cube`.

The cube runtime path already supports landing, walking, picking, edits,
physical coverage streaming, generation and rendering behind that debug entry.
The migration is not complete:

- the M2 baseline still drew the vertical terrain stretch above the player;
- fluid refuses to run on cube surfaces because its queue keys do not include
  the face identity;
- V30 is not implemented, and a cube save cannot yet start in a fresh process
  without the debug runtime gate;
- most ecology, generation, CGA, hydrology and sampling consumers still consume
  map-shaped coordinates or map chunk identity.

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
`/tmp/voxelcraft-cube-vertical.*`. The historical exclusion trail is retained in
[the 2026-10-22 audit](cube-audit-2026-10-22.md). Commit M2.

### M3: close G2 with V30

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

Exit evidence: focused save tests, a two-process round trip, corrupt-file tests,
V29 rejection, undo/redo and unload/reload checks pass. Commit M3.

### M4: close G3 consumer migration

Each item below is a separately reviewable and committed substage.

1. **Fluid:** replace map chunk queue identity with a topology-neutral surface
   chunk identity, add cross-face adjacency and enforce volume conservation.
1. **Water/plant Stage 3:** add deterministic flood tolerance, bounded exposure
   and recovery, plant anchoring and volume-based water queries.
1. **Water/plant Stage 4:** enforce substrate, water, shoreline and rooted
   generation order with deterministic worker-independent output.
1. **Entities and ecology:** move ground queries, spawning and flora to surface
   cell/sample operations.
1. **CGA:** plan and clip structures in surface cells, including face edges.
1. **Volcano/lava/ash:** migrate addresses, history search and section effects.
1. **Hydrology and radiation:** bind regions and activity to surface regions.
1. **Terrain sampling:** migrate remaining height, waterline and bathymetry
   callers to one surface sampling contract.

Exit evidence: Homeworld defaults to cube-grid, planets still pass through the
legacy adapter, consumers have no topology branches, and all Homeworld consumer
scenarios pass. Commit after each substage and a final M4 evidence update.

### M5: close G4

Work:

- make scheduling, admission, eviction, LOD and drawing consume one physical
  coverage result;
- keep the 625-slot nearest-first policy and expose saturation separately from
  local readiness;
- add face, corner, pole, high-altitude and render-distance acceptance scenes;
- record repeated performance and memory measurements.

Exit evidence: Homeworld graphics and performance gates pass on Hyprland. If a
budget fails, record the measured deficit and stop rather than silently adding
capacity. Commit M5.

### M6: close G5

Work:

- remove the Homeworld debug runtime gate and dead Homeworld-only map paths;
- keep the legacy backend only for the explicitly deferred planet migration;
- finish architecture, DSL and documentation cleanup;
- run the complete final gate set.

Exit evidence: every G1-G5 item has current-commit evidence and unknown or
failed checks are listed explicitly. Commit M6.

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
- Code stages run focused tests, `make test`, `make test-architecture` and
  `make check-docs`. DSL changes additionally run `make docs-probe`,
  `make test-debug-cli` and `make test-debug-stdin-protocol`.
- Graphics stages run the Hyprland E2E targets and store deterministic scenes,
  not only interactive observations.
- The final G5 gate runs `make test-sanitize`, `make test-headers`,
  `make test-e2e`, `make test-cube-save-reload`, the surface-topology and polar
  E2E targets, and the debug-graphics E2E target.

