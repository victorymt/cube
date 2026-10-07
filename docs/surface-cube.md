# Polar coverage repair: cube-grid foundation

## Status

The new `surface_cube` module provides pure cube-grid coordinates and exact
cross-face neighbors. **The runtime still uses the legacy map grid; the polar
visual defect is not yet fixed.** This is the first independently testable
implementation step, not a terrain-version change or a save migration.

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

## Remaining integration, in dependency order

1. Add an explicit per-surface topology version to runtime snapshots and
   persistent metadata. Keep Homeworld v7/v8 on its current path. Define the
   old planetary-save policy before changing any address interpretation;
   legacy map cells cannot simply be relabeled as cube cells.
2. Make cube cell identity authoritative for generation, indexing, block
   edits and neighbor lookup. Sample climate/landform using direction or
   latitude/longitude derived from that identity; retain immutable worker
   inputs and stale-job rejection across surface changes.
3. Move player/collision/raycast/fluid coordinates together. Face transitions
   must transport heading and velocity, not just positions. Round-trip edit
   lookup and save/reload must refer to the same cells at every face edge.
4. Use the same neighborhood/coverage policy for scheduling, eviction, LOD
   and drawing. Derive conservative bounds from curved mesh geometry. Keep
   `surface_ready`'s existing local readiness meaning and expose separate
   requested/ready coverage diagnostics instead of claiming visual readiness.
5. Replay the existing temperate and dry polar scenes using geographic
   targets, then cross poles/face edges on foot, edit blocks, unload/reload,
   and save/reload. Preserve old Homeworld samples and enforce memory budgets.

No Coriolis terrain bias is enabled by this repair.
