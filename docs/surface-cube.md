# Polar coverage repair: cube-grid foundation

## Status

The new `surface_cube` module provides pure cube-grid coordinates and exact
cross-face neighbors. **The runtime still uses the legacy map grid; the polar
visual defect is not yet fixed.** This is the first independently testable
implementation step. The topology/save foundation below is now implemented;
terrain generation remains unchanged and no old save migration is provided.

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

## Topology and save contract (V29)

`SurfaceTopologyVersion` carries independent `kind` and `version` fields:
NONE=0/version 0, LEGACY_MAP=1/version 1, CUBE_GRID=2/version 1. These are
independent of terrain generator v7/v8 and the cube-key hash domain version.
Known descriptors are distinguished from runtime support: cube-grid/1 is known
but cannot be installed or loaded yet. Existing map/cube key hashes are unchanged.

Every `WorldSurfaceContext` carries its topology. The world policy contains
Homeworld and planetary defaults; both are legacy-map/1 in this stage, and every
planet inherits the planetary policy. There are no per-body overrides. New-world
reset restores these defaults. Snapshot installation rejects unknown, unsupported,
or active/inactive-inconsistent descriptors without changing state or generation.
Valid snapshots remain value copies and use the existing stale-job generation.

Only V29 world files load. After the 19-byte `VOXELCRAFT_SAVE_V29` magic comes
8-byte `TOPOL001`, followed by four little-endian uint32 values: home kind,
home version, planet kind, planet version. The existing core payload follows.
The current schema-2 spherical trailer keeps canonical map cells authoritative;
a stored face address is not reinterpreted as a cube voxel.

V28 and older require a new world. Loading never migrates or deletes these
files. Format/topology preflight runs before transactional loading or live-state
changes. Later failures restore the complete V29 checkpoint, including policy.
F9/DSL callers reset clocks/input only on successful loads. Unknown topology
versions and known-but-unimplemented cube-grid receive explicit rejection.

Homeworld v7/v8 generation and fingerprints are preserved; that is an algorithm
contract, not old-file compatibility. This stage does not promise a future
coordinate migration for its own legacy-grid saves.

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

- Normal suite: 108 passing binaries; Homeworld v7/v8 terrain fingerprints and
  existing cube-grid/hash regressions remain unchanged.
- ASan/UBSan suite: 108 passing binaries, with no sanitizer errors.
- Public headers (175), architecture checks and debug CLI contract checks pass.
- Hyprland replay passed in `/tmp/voxelcraft-topology-v29.pZZKYB`; `replies.log`
  records V29 Homeworld/planet/space round trips, V18–V28 rejection, invalid and
  unsupported topology rejection, and late trailer failure after loading another
  body's state. Whole-save bytes match before and after both rejection and rollback;
  the two bodies' edits are also checked through the DSL. Rejected input files
  remain byte-identical. This replay does not establish polar visual coverage.

## Remaining integration, in dependency order

1. Make cube cell identity authoritative for generation, indexing, block
   edits and neighbor lookup. Sample climate/landform using direction or
   latitude/longitude derived from that identity; retain immutable worker
   inputs and stale-job rejection across surface changes.
2. Move player/collision/raycast/fluid coordinates together. Face transitions
   must transport heading and velocity, not just positions. Round-trip edit
   lookup and save/reload must refer to the same cells at every face edge.
3. Use the same neighborhood/coverage policy for scheduling, eviction, LOD
   and drawing. Derive conservative bounds from curved mesh geometry. Keep
   `surface_ready`'s existing local readiness meaning and expose separate
   requested/ready coverage diagnostics instead of claiming visual readiness.
4. Replay the existing temperate and dry polar scenes using geographic
   targets, then cross poles/face edges on foot, edit blocks, unload/reload,
   and save/reload. Preserve old Homeworld samples and enforce memory budgets.

No Coriolis terrain bias is enabled by this repair.
