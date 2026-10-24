# Status

The single home for what is true right now. Every other document links here instead of restating it. One physical line per entry; a stale line is a bug.

Updated 2026-10-24. Source of the statements below: the status sections previously duplicated in `README.md`, `todo/00-roadmap.md`, and `docs/surface-cube.md`.

## Current work

**Cube-grid activation, W1 in progress.** G1 (a walkable, editable cube surface with edit round-trip) is not closed. The original expansion and simulation stages are implemented within their recorded scope.

What works today: a cube world is reachable at runtime through `surface debug cube`, which opens a runtime gate, installs a cube policy, and drives streaming from cube coverage. On that path the player lands and walks, block picking is cube-based, and cube edits round-trip through their own save section. Cube-grid has continuous poses, explicit cell/chunk identity, an immutable v9 base generator, shared projected geometry, physical coverage enumeration, and runtime adapters covering generation, in-memory edits, budgeted admission, mesh projection, and render bounds.

**The default runtime still uses the legacy map grid with V29 saves.** Cube-grid is reachable only through that debug entry point and is not a shipping mode. The current save format remains V29.

## Open blockers

| Blocker | Symptom | Owning record |
|---|---|---|
| Cube mesh vertical stretch | The last visual defect blocking G1: terrain above the player renders as vertical streaks | [Audit findings](docs/cube-audit-2026-10-22.md) |
| Fluid on cube surfaces | Open and suspended. The edit queue keys by `(surfaceId, chunkX, chunkZ)` and would cross faces, so fluid returns early on cube worlds | [Activation plan](docs/cube-activation-plan.md) |
| Polar ground coverage | Confirmed a structural limit of the legacy lat/long grid, not an enumeration bug; only the cube migration can fix it | [Cube contract capacity table](docs/cube-contract.md) |

## Activation target

Not the current runtime default. Both Homeworld and planets will switch to cube-grid/1 using cube base 9 plus the historical layer 11, with V30 saves requiring a new world. V30 will reject V29 and older files without migrating or deleting them. Preserving v7/v8 fingerprints is no longer a requirement. Regional ecology payload V7 and terrain generator v11 have independent version sequences. The authoritative version, identity, ownership, and capacity tables live in the [cube activation contract](docs/cube-contract.md).

Next, in dependency order: close the cube mesh vertical stretch, convert the fluid queue to carry `SurfaceCubeChunkKey`, then convert the remaining consumers (ecology, CGA, volcano, terrain height) and implement V30.

## Verified

- 2026-10-24: `make test` passed 126 binaries with 0 failures, and the trailing architecture and clean-build guards passed.
- 2026-10-24: `make test-headers` compiled every public header standalone; `make test-debug-cli` and `make test-debug-stdin-protocol` passed.
- 2026-10-24: `scripts/run-scene.sh` returned `ok=true` for a screenshot scene, an `exit`-terminated scene, and a benchmark scene without captures, and reported `ok=false` with the DSL error for a scene whose command is rejected.
- 2026-10-24: injected layering violations in `gameplay` and `space` were both rejected by `make test-architecture`; `core`, `presentation` and non-`app` upward includes measured zero.
- 2026-10-24: after splitting `world/world.h`, `make test` passed 126 binaries with 0 failures, `make test-headers` compiled 204 public headers standalone, and injected violations of both new include rules were rejected.

## Not verified

Primitives exercised in isolation, and historical test passes on earlier commits, do not establish completion of the normal-game conversion or current visual acceptance. No GPU, occlusion, coverage, or visual acceptance has been re-run since the statements above were written. `benchmark_chunks` does not link (`undefined reference to PlanetSurfaceFractalNoiseAt`); it does that on the pre-split tree too, and `make test` does not cover it.
