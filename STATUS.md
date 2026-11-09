# Status

The single home for what is true right now. Every other document links here instead of restating it. One physical line per entry; a stale line is a bug.

Updated 2026-11-09. Source of the statements below: the status sections previously duplicated in `README.md`, `todo/00-roadmap.md`, and `docs/surface-cube.md`.

## Current work

**Homeworld cube-grid migration, M2 complete; M3 next.** The active sequence and gates live in the [Homeworld cube migration execution plan](docs/cube-migration-execution-plan.md). Planets remain on the legacy backend for this plan.

What works today: both legacy-map and cube-grid implement one topology-neutral `SurfaceContract`; the active contract owns the only long-lived cube generation context, and policy declaration accepts independent or mixed per-surface topologies. A cube world is reachable at runtime through `surface debug cube`, which opens the runtime gate, installs Home cube plus Planets legacy, and drives streaming through the contract's physical-coverage capability. On that path the player lands and walks, block picking is cube-based, and cube edits round-trip through their own save section. Cube-grid has continuous poses, explicit cell/chunk identity, an immutable v9 base generator, shared projected geometry, physical coverage enumeration, and runtime adapters covering generation, in-memory edits, budgeted admission, mesh projection, and render bounds. M2 aligned cube mesh vertices and frustum bounds with the same section anchor frame, eliminating the remaining vertical stretch in the deterministic Hyprland wall replay.

**The default runtime still uses the legacy map grid with V29 saves.** Cube-grid is reachable only through that debug entry point and is not a shipping mode. The current save format remains V29.

## Open blockers

| Blocker | Symptom | Owning record |
|---|---|---|
| Fluid on cube surfaces | Open and suspended. The edit queue keys by `(surfaceId, chunkX, chunkZ)` and would cross faces, so fluid returns early on cube worlds | [Activation plan](docs/cube-activation-plan.md) |
| Polar ground coverage | Confirmed a structural limit of the legacy lat/long grid, not an enumeration bug; only the cube migration can fix it | [Cube contract capacity table](docs/cube-contract.md) |

## Activation target

Not the current runtime default. Both Homeworld and planets will switch to cube-grid/1 using cube base 9 plus the historical layer 11, with V30 saves requiring a new world. V30 will reject V29 and older files without migrating or deleting them. Preserving v7/v8 fingerprints is no longer a requirement. Regional ecology payload V7 and terrain generator v11 have independent version sequences. The authoritative version, identity, ownership, and capacity tables live in the [cube activation contract](docs/cube-contract.md).

Next, in dependency order: implement Homeworld V30 for M3/G2, then migrate fluid and the remaining consumers for M4/G3.

## Verified

- 2026-11-09: M2 closed the cube-only vertical-stretch defect. `test_cube_generation` validates the full anchor-frame-to-camera transform for every mesh vertex, section bounds containment, synthetic and real varied slabs, and distant face-edge coverage. `make test-cube-vertical-geometry-e2e` passed under Hyprland with legacy and cube captures of the same flat vertical wall at camera `0.000000,10.550000,0.000000`; this route does not claim a live cross-face walk or cube polar visual acceptance.
- 2026-11-09: M1 froze the topology-neutral surface contract. `test_surface_contract` passes shared legacy/cube assertions for identity, known/read/write cells, opaque handles, adjacency, samples, regions, coverage and transactional installation; `test_world_surface_snapshot`, `test_save_format`, `test_surface_coverage`, `test_world_environment`, `test_player_collision`, `test_world_block_box`, `test_world_block_landing`, `test_cube_generation`, `make test-headers` (222 headers) and `make test-architecture` pass. The boundary gate permits only exact existing debt entries in `tests/surface_contract_boundary_allowlist.txt`.

- 2026-11-08: water volume is now authoritative per `WorldCell`; air and plant cells can carry water without a legacy `BLOCK_WATER` block, rendering and exposure tests use volume, and `make test` passed 132 binaries with `make docs-probe` resolving 287 fields.
- 2026-10-24: `make test` passed 126 binaries with 0 failures, and the trailing architecture and clean-build guards passed.
- 2026-10-24: `make test-headers` compiled every public header standalone; `make test-debug-cli` and `make test-debug-stdin-protocol` passed.
- 2026-10-24: `scripts/run-scene.sh` returned `ok=true` for a screenshot scene, an `exit`-terminated scene, and a benchmark scene without captures, and reported `ok=false` with the DSL error for a scene whose command is rejected.
- 2026-10-24: injected layering violations in `gameplay` and `space` were both rejected by `make test-architecture`; `core`, `presentation` and non-`app` upward includes measured zero.
- 2026-10-24: after splitting `world/world.h`, `make test` passed 126 binaries with 0 failures, `make test-headers` compiled 204 public headers standalone, and injected violations of both new include rules were rejected.
- 2026-10-24: `make -s benchmark-chunks` links and runs again after the manifest fix, with `mesh_snapshot_bytes=8192`, `solid_vertices_per_chunk=2187.8`, `solid_vertex_reduction_percent=68.5`, `sync_rebuilds=0` and a 53.4 ms median.
- 2026-10-24: `make docs-probe` resolved all 264 documented DSL runtime fields against a live binary and matched the printed ready line; injecting an unknown field and a one-word ready-line change both failed the probe.
- 2026-10-24: `make check-docs` runs both the prose and truth gates and passes; the truth gate reports a stale document when a document's anchors move without it, confirmed by injecting the condition.
- 2026-10-25: `world/world_types.h` no longer exists; `block_type.h`, `block_texture.h`, `flora_types.h`, `chunk_types.h`, `block_edit_types.h` and `terrain_types.h` replace it, with a rule that fails the build if the aggregate returns.
- 2026-10-25: `world/surface_topology.h` rebuilt 169 → 145 objects after `surface_types.h` was split out and made raylib-free; `make test` still passes 126 binaries and `make test-headers` compiles 211 public headers.
- 2026-10-25: `src/core/debug_dsl.c` was split into a parser and a statement executor, and `src/app/game_debug.c` lost its weather dispatch; both are now clear of the architecture file-size limit. `make docs-probe` still resolves all 264 DSL fields afterwards.
- 2026-10-25: CI's `-Werror` build is green again after deleting the dead `GameDebugLocalClimate`; `make test` now runs the same sweep locally (`tests/test_werror.sh`), which a reintroduced unused static fails.
- 2026-10-25: `make test` gained three checks that fail on the states they exist for: the world type layer's leaf/cycle rules, the four benchmark invariants read from `ChunksGetStreamingStats()` rather than a literal, and the public-header test-seam list.
- 2026-10-25: every scene under `tests/scenes/` is now executed by a driver or a make target, enforced by `tests/test_scene_reachability.sh`; three e2e scripts and two flat-baseline routes that previously ran nowhere are wired into `test-e2e` and the Hyprland CI job.
- 2026-10-25: unrecognised command-line options now exit 2 instead of being ignored, and `--perf-report` / `--perf-baseline` reject a missing, empty or repeated value.
- 2026-10-25: `make test` gained `test-declarations`, which requires every `tests/test_*.c` to state what it verifies before its first include; tests predating the rule are listed as removable debt in `tests/test_declarations_allowlist.txt`, and `make test-declarations-backlog` counts what is left.

## Not verified

Primitives exercised in isolation, and historical test passes on earlier commits, do not establish completion of the normal-game conversion or current visual acceptance. No GPU, occlusion, coverage, or visual acceptance has been re-run since the statements above were written. `make test` still does not cover `benchmark_chunks`, so the benchmark is only gated by its own CI job.
