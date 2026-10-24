# world.h responsibility split (2026-10-24)

- Baseline before the split: `b854bdf`
- Method: measured include graph, real rebuild radius from `build/normal/obj/**/*.d`, mutation tests against the new architecture rules
- Nature: **implementation record**. The coupling review that motivated it is [record 21](21-coupling-review-2026-10-24.md), whose numbers describe the state before this change.

## Why

Record 21 identified `world/world.h` as the worst single header in the tree: 58 functions across five unrelated responsibilities, included directly by 61 source files in six components, and 87 of 235 translation units rebuilt by any edit to it.

The cost was not correctness. It was that a consumer wanting one predicate — `IsWaterBlock`, say — compiled against undo history, the torch array, the edit index and the extension hooks as well.

## What changed

`world/world.h` now owns only chunk lifecycle notifications (three functions). Everything else moved into four focused headers:

| Header | Contents | Rebuild radius |
| --- | --- | ---: |
| `world/world_state.h` | seed, terrain mode, reset, cleanup | 59 |
| `world/block_types.h` | name, category predicates, collision/liquid height, palette, material response, `BlockMaterialResponse` | 45 |
| `world/block_edits.h` | block read/write, the persisted edit store, undo history, `WorldMutationSource` | 50 |
| `world/block_lighting.h` | torch emitters and nearby-light queries | 23 |
| `world/world.h` | chunk notifications only, plus it re-exports the four | 19 |

`world.h` dropped from **87 to 19** translation units. It stays as an aggregate so existing callers compile, and `tests/test_architecture.sh` now rejects any new include of it.

`block_types.h` is deliberately a leaf: it includes only `world_types.h` and `raylib.h`, so a renderer can classify and colour block types without reaching edit storage.

## Two type moves

`WorldMutationSource` moved to `block_edits.h` and `BlockMaterialResponse` to `block_types.h`. Both had been declared in `world.h`. Their placement follows their use: the mutation source describes who is writing, the material response describes what a block is made of. Placing `WorldMutationSource` in `world_state.h` first was wrong and `make test-headers` caught it immediately, because `block_edits.h` then referenced an undeclared type.

## Method

`scripts/world-header-usage.py` maps a file's call sites onto the new groups and can rewrite its include. It refuses to rewrite a file that includes the aggregate twice rather than silently merging, which surfaced three real defects:

| File | Defect found |
| --- | --- |
| `presentation/render_sky.c` | second `world.h` include at line 102, mid-file |
| `gameplay/interaction.c` | second `world.h` include at line 24, mid-file |
| `world/terrain.c` | second `world.h` include at line 27, mid-file |

Four files included the aggregate without using any of its symbols (`game_debug_gate.c`, `game_landing.c`, `ecology_flora.c`, `ecology_test_fixture.h`); those includes were removed.

## Guards added

Two rules in `tests/test_architecture.sh`, both verified by mutation:

| Injected violation | Result |
| --- | --- |
| `presentation/render_ui.c` includes `world/world.h` | `include a focused world header, not the world/world.h aggregate` |
| `world/block_types.h` includes `world/block_edits.h` | `src/world/block_types.h must not include a sibling world header` |

The aggregate rule exempts exactly two files: `world/world.c`, which defines the chunk notifications, and `world/chunks_dependencies.h`, the internal chunk facade that legitimately needs all four groups.

## Not done

`world.c` itself is still one 1,160-line file with five cross-group statics (`worldSeed`, `worldTerrainMode`, `blockEditRevision`, `worldExtensionHooks`, `worldMutationSource`) plus the torch and undo arrays. Splitting the implementation is a separate, riskier task: `WorldReset` resets four groups, `WorldCleanup` frees the edit store, and the `SetBlock` path pushes undo records and bumps the revision. The header split already delivers the consumer-facing benefit without touching any of that.

## Verification

```sh
make test-headers          # 204 public headers, each compiled standalone
make test                  # passes; see STATUS.md for the dated result
make test-architecture
python3 scripts/world-header-usage.py FILE...   # per-file header report
```

`make test-headers` is the key gate here: it compiles every public header alone, which is what rejected the first `WorldMutationSource` placement.

One unrelated failure surfaced while building every test binary by hand: `benchmark_chunks` fails to link with `undefined reference to PlanetSurfaceFractalNoiseAt`. It reproduces on the pre-split tree, so it predates this change and is not covered by `make test`.
