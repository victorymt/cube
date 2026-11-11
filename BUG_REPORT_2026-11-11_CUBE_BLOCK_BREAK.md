# Bug report: cube-grid block destruction leaves the visible mesh stale

- Date: 2026-11-11
- Status: open, not fixed
- Severity: high (block destruction is visibly unreliable in the default Homeworld topology)
- Scope: Homeworld cube-grid startup/transition, block interaction, chunk rebuild, debug readiness and E2E coverage

## Summary

The cube raycast and the authoritative cube-cell write are working. A real left click resolves a `WorldBlockAddress`, removes that cube cell, and a later raycast/cell inspection sees air.

The visible result is still wrong because a cube world also has legacy flat chunks loaded. Both chunk sets are rendered, while the post-click priority rebuild and the debug `mouse_left_mesh_ready` probe still resolve chunks through legacy map coordinates. The cube mesh can be rebuilt by the global dirty scan, but the unchanged legacy mesh remains drawn over it. Separately, the DSL `block set` command writes through the legacy path, so the existing E2E scene builds its wall in one topology and then tests ray interaction in the other topology.

This is consistent with an incomplete legacy-to-cube-grid migration, not a ray-intersection failure.

## Reproduction

### Normal cube world, real click

A fresh world was started normally with `terrain flat`; no `surface debug cube` transition was used. The DSL then waited for `world.surface_topology == "cube-grid"`, aimed at the ground, captured a screenshot, queued one real left click, waited 120 input frames, and captured a second screenshot.

Observed result:

| Check | Before click | After click |
| --- | --- | --- |
| `render pick` target | Grass at local `(-1, 8, 2)` | Dirt at local `(-1, 7, 2)` |
| `cell inspect` of original hit | Grass | `block="Air"` |
| Cube mesh jobs | initial mesh set | global dirty scan submitted additional cube mesh jobs |
| Target-area image | normal grass surface | visually unchanged grass surface |

The center crop of the 1280x720 captures had the same appearance before and after. Mean RGB over a 100x80 target-centered crop changed only from `(92.8, 111.6, 79.2)` to `(92.1, 111.6, 79.2)`. Removing the top cell should expose a visibly different cell/side, not leave the rendered surface effectively identical.

Reproduction artifacts:

- `/tmp/cube-break-pure`
- `/tmp/cube-break-pure/screenshots/voxelcraft_20261111_212149_000.png`
- `/tmp/cube-break-pure/screenshots/voxelcraft_20261111_212150_000.png`
- `/tmp/cube-break-pure/trace.jsonl`

### Runtime chunk topology at the click

A current debug build was stopped at `GameCollectHitBlock` for the same reproduction. The process had both topologies loaded at once:

```text
COUNTS legacy=9 cube=4
```

The authoritative hit was valid:

```text
hit->hasAddress = true
hit->address.at.surface = { bodyId=0, face=SURFACE_FACE_POS_X,
                            u=2048, v=2046, radial=8 }
hit->x= -1, hit->y=8, hit->z=2
```

The local `x/z` values are only the canonical map-coordinate presentation of the cube hit. They are not the cube chunk identity.

At the post-click priority rebuild:

```text
RebuildDirtyChunkMeshAt(x=-1, y=8, z=2)
FindChunk(x / 16, z / 16) -> slot 0 legacy chunk (cubeGrid=false, cx=0, cz=0)
```

The actual address belongs to cube chunk key `{face=0, u=128, v=127}` (`2048/16`, `2046/16`), not legacy chunk `(0,0)`. GDB confirmed that the later global scan submits dirty cube jobs, for example:

```text
SUBMIT cube=1 spherical=1 cx=127 cz=127 dirty=1 stamp=5
```

The cube rebuild path therefore exists, but it is reached only through the global fallback scan, not through the address-preserving priority path.

## Root cause

### 1. Legacy chunks are created in cube worlds and never retired

- `BeginNewWorld()` installs the default cube-grid topology and contract, then still calls `UpdateChunks()` unconditionally (`src/app/game.c:220-225`).
- The debug cube transition also calls `UpdateChunks()` unconditionally after switching topology (`src/app/game_world_transition.c:261-265`).
- `UpdateChunks()` is the legacy map-chunk streaming path. On a physical cube contract, `GameUpdateWorldStreaming()` later uses surface coverage instead and returns early (`src/app/game_streaming.c:30-41`), but it does not unload the legacy chunks already created.
- A normal cube session therefore contains both nine legacy chunks and four cube chunks.

### 2. The renderer draws both copies

`CollectSurfaceRenderItems()` iterates every loaded chunk without requiring the chunk topology to match the current surface topology (`src/presentation/render_scene.c:122-190`). `ChunkWithinCoverageAdmission()` admits all non-cube chunks unconditionally (`src/world/chunks_runtime.c:925-929`). The shadow pass does the same (`src/presentation/render_scene.c:424-487`).

As a result, the legacy surface mesh continues to cover the hole produced in the cube mesh.

### 3. Mesh priority loses the authoritative address

`GameCollectHitBlock()` correctly writes with `SurfaceContractCellWrite(... hit->address ...)` (`src/app/game_interaction.c:177-201`). After success, however, `GamePrioritizeInteractionMesh()` receives only `hit.x/hit.y/hit.z` (`src/app/game_interaction.c:306-307`; struct at `src/app/game_interaction.h:25-28`).

`RebuildDirtyChunkMeshAt()` converts those local map integers with `WorldToChunkLocal()` and calls `FindChunk()` (`src/world/chunks_runtime.c:879-893`). `FindChunk()` resolves legacy map chunks (`src/world/chunks_storage.c:501-506`), not `FindCubeChunk()`. The priority request therefore targets an unrelated legacy chunk and usually returns without rebuilding the just-edited cube chunk.

### 4. Debug mesh readiness reports the wrong pipeline

`GameUpdateDebugLeftMeshReady()` performs the same legacy `FindChunk()` lookup using `debugLeftTargetX/Z` (`src/app/game.c:719-735`). When the unrelated legacy section is already clean, it sets `interaction.mouse_left_mesh_ready` in the same frame as the click. In the existing E2E trace, `mouse_left_set_block` and `mouse_left_mesh_ready` occur on the same frame. That readiness flag does not mean the cube mesh has uploaded.

### 5. DSL block setup writes the legacy copy

`BlockSet()` calls the ordinary `SetBlock()` API (`src/app/game_debug_block.c:187-232`). In the mixed runtime, `SetBlockCore()` finds the legacy chunk first and edits that copy, while `CellInspect()` reads through the active `SurfaceContract` (`src/app/game_debug_block.c:234-267`).

A direct probe showed:

```text
block set ok raw=0,9,8 ... previous_name="Air" id=3 name="Stone"
cell inspect ok position=0,9,8 known=1 block_id=0 block="Air"
```

The successful `block set` response is therefore misleading in a cube world. The wall visible in the existing cube replay is rendered from the legacy copy, not from cube cells.

### 6. The E2E test validates state identity, not the broken visual path

`tests/scenes/cube_vertical_geometry_replay.dsl:43-99` switches to cube, uses `block set` to construct a wall, ray-picks the ground, removes one ground block, and then only checks that the same address is no longer picked. The shell test checks screenshot counts, camera pose and the presence of `world=cube` (`tests/test_cube_vertical_geometry_e2e.sh:20-38`).

It does not verify:

- that `block set` changed a cube cell;
- that no legacy chunks remain in cube mode;
- that the selected target's cube chunk became dirty;
- that the correct cube model stamp advanced;
- that the rendered image changed.

That is why `make test-cube-vertical-geometry-e2e` passes while real block destruction remains visually stale.

## Expected behavior

For a cube-grid surface:

1. Only chunks belonging to the active topology participate in streaming, rendering, shadows, diagnostics and block rebuild priority.
2. Left click must preserve the `WorldBlockAddress` through the post-edit mesh scheduling path.
3. `interaction.mouse_left_mesh_ready` must be tied to the cube chunk key plus the relevant section/model stamp, not a map-coordinate legacy chunk.
4. DSL edit commands must resolve map positions through the active `SurfaceContract`, or expose explicit address-based `cell set`; they must not report success when the active cube cell was unchanged.
5. A block destruction E2E must assert both authoritative cell state and a changed rendered target, with screenshots before and after the mesh upload.

## Suggested fix order (not applied)

1. Gate the legacy `UpdateChunks()` calls in `BeginNewWorld()` and `GameWorldTransitionDebugSurface()` on the contract not supporting physical coverage. When cube coverage is active, use only the cube coverage scheduler.
2. On topology switch/startup, explicitly release every chunk from the previous topology so legacy and cube chunks cannot coexist.
3. Carry `WorldBlockAddress` through `GameInteractionContext` mesh-priority state and add an address-aware rebuild function using `SurfaceCubeChunkAt()` plus `FindCubeChunk()`.
4. Track debug mesh readiness by cube key, `dirtyStamp`, and the exact/LOD model stamp rather than `FindChunk()`.
5. Route `block set` through `SurfaceContractCellWrite()` for the active surface, or add an address-native `cell set`; make mismatched success impossible.
6. Strengthen the E2E to assert `cell inspect` after setup, no legacy chunks in cube mode, correct cube model stamp progression, target-address disappearance, and visual/image evidence.

## Non-findings

- `CubeRaycast()` is finding the correct cube cell in the reproduced case.
- `HitResult.hasAddress` survives the raycast result.
- `GameCollectHitBlock()` writes through the active cube contract and the authoritative cell becomes air.
- The global dirty-mesh scan does submit cube rebuild work.

These parts work; the failure boundary is topology coexistence, address-dropping chunk selection, and debug/test paths that inspect different data copies.
