# Cube-grid activation: foundation and pending runtime integration

## Status

> Version, identity, ownership and capacity facts are frozen in the
> [cube activation contract](cube-contract.md). This document keeps the
> narrative and stage history; where the two disagree, the contract wins.

Current state: see [STATUS.md](../STATUS.md).

Primitives exercised in isolation must not be mistaken for completion of the
normal-game conversion.

## Root cause and scope

`CollectMissingChunkCandidates` enumerates a fixed square of longitude/latitude
map chunks. `CurveSurfacePoint` projects its vertices to a sphere. At the
captured latitude (89.967 degrees), the longitudinal metric is about 0.000575:
a 16-map-cell edge spans less than 0.02 physical blocks. Both dry-planet
controls reproduce the problem without a landform atlas.

`ChunkGridDistanceFrom` already uses spherical shortest displacement through
`SurfaceShortestMapOffset`. The repair must address candidate coverage rather
than replace it on the assumption that it uses raw map distance. Existing
distance users (retention, LOD, drawing) must later consume the new grid's
actual projected footprint consistently.

## New contract

- `SurfaceCubeChunkKey` identifies a surface column by body, face and integer
  face-chunk U/V. It is a separate type from the legacy map-corner key. The
  stable hash includes a topology-version domain and explicit byte order.
- `SurfaceCubeCellNeighbor` and `SurfaceCubeChunkNeighbor` fold a cardinal
  step with exact integer orientation/reversal. The returned reverse edge is
  in the destination face's coordinates. Floating-point projection followed
  by rounding is unsuitable for block/fluid adjacency because it need not be
  bijective. A diagonal near a cube corner must not assume U/V steps commute.
- `SurfaceCubeVertex` evaluates continuous vertex coordinates, including
  face boundaries. Cell centers remain `(u + .5, v + .5)`. The projection and
  face axes agree with existing `SurfaceAddressDirection`.
- `SurfaceCubeNeighborhood` enumerates a deterministic, unique four-neighbor
  graph ball through edges and corners. Graph distance is not physical block
  distance. At radius 12 the worst case is 313 columns. It rejects inadequate
  capacity rather than returning a misleading partial coverage result.

The neighborhood is a bounded graph stencil, not a camera-frustum coverage
certificate. Perspective, camera altitude, physical distance, and terrain
height will determine the final runtime policy. Generation scheduling should
cache the stencil until the focus or radius changes; the small pure reference
enumerator currently uses a linear duplicate check.

## Validation

```sh
make test-surface-cube
```

The regression first measures the collapsed legacy edge. It then checks:

- All 98,304 directed face-boundary cell transitions, including corner cells:
  exact return identity, body/radial preservation, shared vertex positions,
  and consistency between cell and chunk neighbors.
- Face-grid projected 16-cell edges across all six faces: sampled widths are
  9.628–20.371 blocks, with no polar singularity. This is a bounded-distortion
  cube projection, not an equal-area or exactly one-meter voxel projection.
- Radius-12 neighborhoods at face centers, edges and corners: stable order,
  no duplicates, complete cardinal expansion and bounded count.
- Both pole neighborhoods: 25 columns at radius 3 span about 122.19 blocks in
  each tangent direction, in contrast to the collapsed legacy stencil.
- Invalid coordinates, nonfinite projection inputs, capacity rejection and a
  pinned hash vector.

## Topology and save contract (V30)

`SurfaceTopologyVersion` carries independent `kind` and `version` fields:
NONE=0/version 0, LEGACY_MAP=1/version 1, CUBE_GRID=2/version 1. These are
independent of the terrain generator version (currently 11) and the cube-key
hash domain version. See the [activation contract](cube-contract.md) for the
full version matrix.
Known descriptors are distinguished from runtime support: cube-grid/1 is known,
and the debug activation path admits it only while the cube runtime gate is
open. A V30 header may restore its saved topology during the transactional load
path, so a fresh process does not require the debug gate. Existing map/cube key
hashes are unchanged.

Every `WorldSurfaceContext` carries its topology. The world policy contains
Homeworld and planetary defaults: Home is cube-grid/1 and planets are
legacy-map/1. Every planet inherits the planetary policy; there are no per-body
overrides. New-world reset restores these defaults, while `surface debug home`
can install an explicit legacy Home baseline and `surface debug cube` can switch
back. Normal composition opens the cube runtime gate before creating the Home
contract, and V30 load installs the saved policy first. Snapshot installation
rejects unknown, unsupported, or active/inactive-inconsistent descriptors
without changing state or generation. Valid snapshots remain value copies and
use the existing stale-job generation.

Only V30 world files load. After the 19-byte `VOXELCRAFT_SAVE_V30` magic comes
8-byte `TOPOL002`, followed by four little-endian uint32 values: home kind,
home version, planet kind, planet version. The core then stores canonical
`SurfacePose` and a `CUBADDR2` block of `WorldBlockAddress + WorldCell` records.
The old map-space spherical trailer is no longer the authority for surface
identity.

V29 and older require a new world. Loading never migrates or deletes these
files. Format/topology preflight runs before transactional loading or live-state
changes. Later failures restore the complete V30 checkpoint, including policy.
Fresh processes install cube backend from the header without the debug gate.
F9/DSL callers reset clocks/input only on successful loads.

The legacy map grid uses terrain generation version 11 (which includes the v7
drainage base, v8 river terraces and v11 historical volcanic layers). The
approved activation target below supersedes preservation of those fingerprints;
V29 legacy-grid saves will not be migrated to that target. Which grid the
running game uses is recorded in [STATUS.md](../STATUS.md).

### Validation commands

```sh
make test
make test-sanitize
make test-surface-topology-e2e
```

Focused tests cover descriptor validation, rejected-install atomicity, pinned LE
header bytes, truncation/unknown/cube rejection and schema-1 trailer rejection.
The DSL replay covers Homeworld/planet/space round trips, all V18–V28 headers,
unsupported topology, late corrupt-trailer rollback and retained block edits.
Read-only `world.surface_topology`/`world.surface_topology_version` report the
active dimension's descriptor (none/0 in space and Nether).

### Verification evidence (2026-10-08)

- Historical record: 108 passing binaries in that session; Homeworld v7/v8
  terrain fingerprints and
  existing cube-grid/hash regressions remain unchanged.
- ASan/UBSan suite: 108 passing binaries, with no sanitizer errors.
- Public headers (175), architecture checks and debug CLI contract checks pass.
- Hyprland replay passed in `/tmp/voxelcraft-topology-v29.pZZKYB`; `replies.log`
  records V30 Homeworld/planet/space round trips, V18–V29 rejection, invalid and
  unsupported topology rejection, and late trailer failure after loading another
  body's state. Whole-save bytes match before and after both rejection and rollback;
  the two bodies' edits are also checked through the DSL. Rejected input files
  remain byte-identical. This replay does not establish polar visual coverage.

## Remaining integration, in dependency order

### Full activation work in progress

The activation target is now cube-grid/1 on both Homeworld and planets,
terrain generation v9, and V30 saves without migration from V29 or earlier.
The previous v7/v8 fingerprint preservation requirement does not apply to
that target. These defaults are **not activated** until the runtime consumers
below have been converted together.

Implemented coordinate and generation primitives:

- `SurfacePose` stores double-precision body-centered position, velocity and
  tangent heading. Geographic initialization and great-circle displacement
  carry heading and velocity continuously across poles and face edges.
- Continuous cube projection and inverse projection agree on X/Y/Z dominant
  axis tie-breaking; cells use center sampling and explicit radial height.
- `WorldBlockAddress` tags surface addresses separately from Space/Nether
  Cartesian coordinates. Hashing excludes padding and inactive union bytes.
  Six-sided neighbors preserve body identity and reject integer overflow.
- Cardinal orientation transport is invertible across every directed face
  boundary, including corner cells. Multi-step corner paths remain ordered.
- `TerrainCubeContext` owns a copied surface snapshot and an immutable atlas;
  retained worker references keep them alive after the owning world releases
  its reference. Sampling reads explicit direction/seed/profile, including
  caves, and does not use mutable world state. Column samples are tied to
  their context and world generation. New v9 base-material fingerprints are
  pinned for Homeworld and a dry planet; decoration/ecology is still pending.
- The v9 shell starts at radial -2048. The old Homeworld's maximum water depth
  (10,900) exceeds the globe radius (~2,607): depths below the first 200 blocks
  are compressed, keeping the deepest baseline 32 blocks above the shell floor.
  The atlas and voxel sampler use the same adjusted baseline.
- Cube staging columns use existing sparse `ChunkSection` storage, resolving
  air without allocating it and generating deep sections on demand. The real
  chunk index distinguishes cube keys, legacy map keys and Cartesian keys;
  lookup also checks the current slot occupant after reuse.
- `SurfaceCellTriangles` provides the same outward-facing projected geometry
  to ray and continuous box-sweep queries, including fractional block shapes.
  At a box/triangle edge contact the separating normal determines support;
  the triangle's side ID alone does not.
- `SurfaceCoverageCollect` enumerates a physical spherical cap across faces,
  sorts deterministically and reports requested/admitted counts and saturation.
  At the maximum 208-block request the tested corner cases require up to
  1,560 candidates, exceeding the runtime's 625 slots. Saturation is explicit.
  Conservative curved-patch bounds include the full radial interval. This is
  ground coverage, not a high-altitude camera-frustum coverage certificate.

`make test-surface-cube` exercises these primitives, including 20,000 movement
steps around a full meridian, cross-body chunk index separation, generated
sections, worker input lifetime, ray/sweep geometry and independently sampled
coverage. These tests do not establish runtime activation or visual coverage.

Verification of this foundation (2026-10-08): all 113 normal test binaries and
all 113 ASan/UBSan test binaries passed, along with 180 public headers, complete
module/game builds, debug CLI/protocol contracts and architecture checks.
LeakSanitizer required execution outside the ptrace-based sandbox. No polar
visual acceptance is claimed for these changes.

### Runtime consumers still to convert

Progress as of 2026-10-23 is marked per item; unmarked text is the original
2026-10-08 assessment.

1. Connect runtime coverage to the cube column/section request API described
   below. Cube edit storage and generation replay are implemented, but
   **partially done**: `ChunksUpdateCubeCoverage` now drives streaming on the
   cube path and player edits write cube storage. Decorations and ecology still
   require conversion; fluid now routes section lookup and cross-face
   neighbours through the surface contract.
2. Move player/collision/raycast/fluid coordinates together. Face transitions
   must transport heading and velocity, not just positions. Round-trip edit
   lookup and save/reload must refer to the same cells at every face edge.
   **Done for landing, walking, collision, raycast, edit storage, fluid and V30
   fresh-process restore.**
3. Use the same neighborhood/coverage policy for scheduling, eviction, LOD
   and drawing. Derive conservative bounds from curved mesh geometry. Keep
   `surface_ready`'s existing local readiness meaning and expose separate
   requested/ready coverage diagnostics instead of claiming visual readiness.
   **Diagnostics are exposed** (`stream.cube.*`); LOD and drawing policy are
   still not unified. The M2 transform/bounds repair now makes mesh vertices and
   frustum centers use the same section anchor frame, closing the vertical
   stretch.
4. Replay the existing temperate and dry polar scenes using geographic
   targets, then cross poles/face edges on foot, edit blocks, unload/reload,
   and save/reload. Pin the v9 output and enforce memory budgets. Complete V30
   save/reload coverage and the remaining consumer migration before enabling
   the defaults.
   **Not started.**

No Coriolis terrain bias is enabled by this repair.

### Runtime stage 1: generation queue adapter (2026-10-14)

The production worker queue can now execute v9 cube columns and sections using
`TerrainCubePrepareJob` and `QueueCubeChunkGenJob`. This is an internal adapter;
normal `UpdateChunks` still schedules legacy columns. It does not enable
cube-grid, change save defaults, or close polar visual acceptance.

- A prepared cube job owns one reference to its immutable terrain context.
  Successful submission moves that ownership into the queue; failed submission
  leaves it with the caller. The worker uses the retained input rather than
  reading the live world to generate terrain.
- Completion checks body, face, U/V, chunk incarnation, grid namespace and
  world generation. Columns and sections share this check. Stale results never
  replace a different face, body, or reused slot.
- Completion, rejected results, queued cancellation, shutdown and scheduler
  reset release the input. Failed column generation releases its destination
  reservation. Air-only sections retain a resolved marker without allocating
  block storage.
- Generated cube sections dirty their actual cube neighbors, including face
  boundaries. Cube completion deliberately excludes legacy map-coordinate edit
  application and ecology notifications; those consumers must be converted
  before activation. Legacy scheduling, meshing and coverage remain in use.

`tests/test_cube_generation.c` exercises the real terrain generator, storage,
worker queue and completion path with two pthread workers. It covers all six
faces, deep bedrock, implicit air, owner release before worker completion,
duplicate submission, queue capacity, queued cancellation, missing workers,
allocation failure and stale result rejection. Its game environment is a
neutral fixture; it is not an in-game cube-grid acceptance test.

Validation: `make -j12 test all` passed 120 test binaries, public-header checks, <!-- volatile-counts:allow -->
module/game builds, debug CLI/protocol checks and architecture/clean-build
guards. The final focused rerun passed all eight `make test-surface-cube`
binaries and the legacy chunk-streaming test. The cube generation queue test
also passed ASan/UBSan with leak detection enabled.

Polar baseline: the first `test-polar-e2e` run timed out at 90 seconds while
the full suite was running (`/tmp/voxelcraft-polar-e2e.ocPGrS`). After the suite
finished, the independent rerun completed all 600 input frames in 15 seconds
(180-second budget), with artifacts in `/tmp/voxelcraft-polar-e2e.6NHoI5`.
Visual inspection at the time still showed the narrow vertical terrain strip.
The protocol passes, but polar ground coverage remains visibly broken. This
capture was a pre-activation baseline, not a successful visual acceptance
result. (The original PNG lived in the untracked `screenshots/` directory and
was discarded during a 2026-10-23 workspace cleanup; the finding above is kept
because the defect it describes is still open and reproduced by
`test-polar-e2e`.)

Stage 2 below implements cube-addressed edits and runtime allocation. The
remaining activation work includes ecology/decorations and shared
physical coverage.

### Runtime stage 2: cube edits and column admission (2026-10-14)

`world_cube.h` now exposes main-thread edits keyed by `WorldBlockAddress`.
Homeworld uses body 0 and planetary surfaces use nonzero body identities.
Space/Nether Cartesian addresses, invalid cells/heights and invalid materials
are rejected. The cell hash index supports replacement without duplicate edits;
a second chunk index limits generation replay to that column's edits. Growth
allocates replacement storage before publishing it, with an explicit limit of
1,048,576 edits. World reset and cleanup clear the store.

`WorldCubeBlockAt` returns false for unknown, unresolved terrain, rather than
assuming air. Explicit edits are readable even when the column is unloaded.
Resident resolved sections update immediately, with cross-face dirty propagation.
Edits to ungenerated deep sections stay in the edit store without materializing
an incomplete terrain section. Column and section completion apply the latest
main-thread edits before publication, including edits made during worker execution.

`chunks_cube.h` provides column requests, section requests and explicit release:

- Requests use the production chunk pool and worker queue, with an equivalent
  synchronous path when workers are unavailable. Results distinguish ready,
  pending and unavailable; duplicate requests do not reserve extra slots.
- Allocation uses only free slots. Saturation does not silently evict existing
  columns; the upcoming physical-coverage scheduler owns eviction policy.
- Context generation and body identity are checked before admission. Resident
  cube columns retain their world generation, so old snapshot data cannot be
  mistaken for current terrain. Preload promotion survives worker completion.
- Release invalidates the chunk incarnation and cancels queued tasks. Running
  jobs retain their input until completion and fail the stale-result guard if
  their slot has been reused. Edits survive release and subsequent regeneration.

The expanded `test_cube_generation` fixture covers hash-index growth/replacement,
invalid-address rejection, reserve failure without changing resident terrain,
cross-face/body isolation, deep edits and air-only sections, unload/regenerate,
edits during real asynchronous work, stale context rejection, slot reuse,
preload promotion and pool saturation without eviction.

Validation: `make -j12 test all` passed all 120 binaries, public-header and
module/game builds, debug CLI/protocol contracts and architecture/clean-build
checks. The final preload-promotion revision passed the focused queue/runtime
test and all eight `make test-surface-cube` binaries. The expanded queue/runtime
test also passed ASan/UBSan with leak detection enabled; the final game rebuild
and architecture check passed without compiler warnings. No graphics replay
was used to claim coverage for these data/queue changes.

At the end of this stage, cube-grid was **not** active in normal gameplay and the
new edit store was in memory only. V30 serialization, undo/redo, player
interaction, fluid/ecology notifications, decorations and mesh/rendering
conversion were still pending. M3 later completed V30 persistence and undo/redo;
the other items remain tracked by the migration execution plan. This stage did
not change or re-certify polar visual coverage.

### Runtime stage 3: physical admission and mesh projection (2026-10-14)

Full normal-game integration was selected as the activation gate: the default
must remain legacy until movement, collision, interactions, simulation and V30
persistence are converted and polar gameplay is accepted. No preview mode or
partial default activation was introduced.

`ChunksUpdateCubeCoverage` now owns nearest-first admission and eviction for an
explicit body-centered focus and physical radius. It shares
`SurfaceCoverageCollect`'s cap and capacity policy, rejects stale/invalid input
before mutation, releases stale or out-of-cap cube residents, and preserves
other grid namespaces. Its per-call submission budget includes local radial
section requests. Diagnostics distinguish requested/admitted/ready/pending/missing
columns, submissions, releases and saturation. Column readiness does not assert
collision-layer or visual readiness. The caller still needs to supply the active
cube terrain context; normal `UpdateChunks` remains on the legacy path.

The production synchronous and worker mesh paths now accept cube chunks:

- Main-thread boundary snapshots sample exact cube neighbors, including axis
  transport at corners. Missing terrain remains unknown. Workers consume copied
  blocks/volumes and never dereference resident neighbor columns.
- Vertices project face U/V plus radial height into body axes relative to a
  double-precision section anchor. Triangle normals are recomputed after
  projection. Exact solid faces use one-cell spans. The shared collision/ray
  geometry now uses the renderer's top-face diagonal, since a projected radial
  quad is not planar.
- Mesh completion validates cube namespace, body/face/U/V, world generation,
  slot incarnation and content stamp. Results for stale or different faces
  cannot overwrite the current section.
- Terrain/water and shadow rendering have a cube section transform and
  conservative curved-patch bounds. Draw-distance checks share admission's
  conservative physical footprint metric. Chunk distance uses physical angular
  separation for cube residents rather than treating face indices as map
  coordinates. General coarse LOD selection remains available, but its visual
  behavior and scheduling policy are not yet accepted in cube gameplay.

The generation fixture now tests budgeted polar coverage, opposite-pole eviction,
capacity saturation, invalid/stale input atomicity, all 24 directed face-edge
halo transitions, six-face mesh projection, renderer/collision top diagonals,
render transforms and conservative bounds, and stale mesh rejection. These are
CPU tests of the production pipeline, not an in-game polar visual acceptance.

M1 now supplies one topology-neutral `SurfaceContract` for legacy and cube,
with opaque chunk/section handles, complete `WorldCell` I/O, semantic
neighbours, samples, regions and physical coverage. The active contract is the
single long-lived `TerrainCubeContext` owner; player collision and main
streaming use contract capabilities instead of topology branches.

M2 closed the remaining vertical stretch by applying one section-anchor tangent
frame consistently to mesh vertices, render transforms and frustum bounds. The
new `cube_vertical_geometry_replay.dsl` scene captures the same flat vertical
wall through legacy and cube under Hyprland. That route isolates vertical
geometry and bounds; live cross-face walking and cube polar visual acceptance
remain separate gates. Ecology/decoration, the remaining diagnostic
consumers and the normal streaming default still require migration before
cube-grid becomes the default.

Validation: the full `make -j10 test all` run passed all 120 binaries, 193
public headers, module/game builds, CLI/protocol checks and architecture guards.
Final projection/geometry/distance revisions passed the eight cube-grid suites
and the chunk-streaming regression. The expanded cube pipeline test passed
ASan/UBSan with leak detection. No cube-grid gameplay or polar screenshot
acceptance is claimed by this stage.
