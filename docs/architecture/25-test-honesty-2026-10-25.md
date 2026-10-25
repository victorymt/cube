# Test honesty pass (2026-10-25)

- Baseline before this work: `1cf692b`
- Method: CI-flag build of the whole tree, mutation tests against every new check, execution of every e2e script in the tree
- Nature: **implementation record**. It is mostly a list of things that were not being tested despite appearing to be, which is why it names each one.

## Why

A green `make test` says the assertions that exist passed. It says nothing about whether the right assertions exist. This pass found four ways the suite was quieter than it looked:

1. CI was failing and local runs could not see it.
2. A gate was guarding a version of the code that no longer existed.
3. A documented invariant was a literal.
4. Working tests existed that nothing invoked.

## CI was red, and the local run could not tell

`make CFLAGS='-std=c99 -Wall -Wextra -Werror -O2 -pthread' test` exited 2:

```
src/app/game_debug.c:827:13: error: 'GameDebugLocalClimate' defined but not used
```

`ea87547` moved the weather dispatch into `game_debug_weather.c`, which already had an identical `WeatherLocalClimate`. The old static stayed behind in `game_debug.c` and became unused.

Locally `make test` passed, because `Makefile:4` sets `-std=c99 -Wall -Wextra -O2 -pthread` without `-Werror`; only CI supplied it. A warning therefore had to travel to CI to be seen.

Fixed two ways: the dead function is deleted, and `tests/test_werror.sh` compiles every file under `src/` with the CI flags. It runs eight at a time and costs about six seconds, so it is part of `make test` now. Injecting an unused static makes it fail, naming the file and the warning.

`tests/*.c` is deliberately excluded from the sweep: `mk/tests.mk` gives each test its own `-D*_TESTING` macro set, and compiling a test file without those macros reports implicit declarations that are not defects.

## The gate guarded the previous split

`tests/test_architecture.sh` had a "focused headers must stay independent" loop covering `world_state.h`, `block_types.h`, `block_lighting.h` and `world_state.h`'s neighbours — the four headers from the `world.h` split recorded in [record 22](22-world-header-split-2026-10-24.md).

The seven headers the type split produced (`block_type.h`, `block_texture.h`, `block_edit_types.h`, `flora_types.h`, `terrain_types.h`, `surface_types.h`, `chunk_types.h`) were not covered by any rule. Injecting `#include "world/chunk_types.h"` into `block_type.h` — undoing the point of the split — passed the entire gate silently.

Two invariants now cover the layer:

- the three leaf headers (`block_type.h`, `terrain_types.h`, `surface_types.h`) include no sibling at all;
- no header in the layer reaches itself, checked by a bounded transitive walk.

Both are mutation-tested: a leaf edge and a `flora_types.h ↔ chunk_types.h` cycle each fail with a message naming the header. A legitimate DAG edge, such as `flora_types.h → world_state.h`, still passes — the rule is about cycles and leaves, not about forbidding every edge.

## The documented invariant was a constant

`docs/terrain-performance.md` and three other documents quote four values as invariants that must not move. Three are computed from the mesh the benchmark builds. The fourth came from this line:

```c
printf("sync_rebuilds=0\n");
```

The benchmark never touches the streaming scheduler, so zero was the honest answer — but it was going to print zero regardless of what any future change did. A regression that introduced synchronous rebuilds into the benchmark path would have been invisible.

The line now reads `ChunksGetStreamingStats().syncRebuilds`. Injecting a value of 7 into the accessor makes the benchmark print `sync_rebuilds=7`, which is what makes it a measurement.

`tests/test_benchmark_invariants.sh` runs the benchmark and asserts the four documented values, and it also rejects a return to the literal form, because that is the specific regression that shipped.

## Tests that existed and ran nowhere

Three e2e scripts had no make target, no CI entry, and nothing sourcing them: `test_atmosphere_e2e.sh`, `test_landform_planets_e2e.sh`, `test_water_section_e2e.sh`. All three passed when run by hand. They are the only coverage of stratified atmosphere assertions, planet landform replay across body switches, and water section geometry.

Two more scenes were in the same state: `flat_static_benchmark.dsl` and `flat_rotation_benchmark.dsl`, whose only reference was the shell command printed in `docs/performance/flat-rotation-baseline.md`. `volcano_menu.dsl` was cited once in `docs/debug-dsl.md` as a manual command.

All six are wired up now. `tests/test_scene_reachability.sh` fails when a scene in `tests/scenes/` is executed by nothing — documentation does not count, because citing a scene is exactly what those two had. It found the three scenes that were still unreferenced after the first pass.

## Also fixed

- **Unknown options were ignored.** `--bogus-flag` exited 0. `--perf-report` and `--perf-baseline` accepted a missing, empty or repeated value while every other path option rejected them. Both now fail with exit 2 and a message naming the option, with the validation moved into `game_runtime.c` next to the parsers (it briefly pushed `game.c` past its 1500-line limit).
- **`rg` was an undeclared dependency.** Six e2e scripts used ripgrep; CI's apt list does not install it and no document mentions it. The three new drivers use `grep`.
- **Public header seams are pinned.** Fifteen public headers expose an `#ifdef *_TESTING` block. That is a deliberate widening of the contract, and `tests/test_public_header_seams.sh` now requires the sixteenth to be added on purpose.

## What the new checks cost

| Check | Time | In `make test` |
| --- | --- | --- |
| `test_werror.sh` | ~6 s (parallel) | yes |
| `test_benchmark_invariants.sh` | ~1 s after build | yes |
| `test_public_header_seams.sh` | <0.1 s | yes |
| `test_scene_reachability.sh` | <0.1 s | yes |
| `test_debug_fields_e2e.sh` | ~10 s, 24-field sample | Hyprland E2E only |
| `test_flat_baseline_e2e.sh` | ~2 min | Hyprland E2E only |
| the three revived e2e scripts | several minutes each | Hyprland E2E only |

`test_debug_fields_e2e.sh` is the subset of `make docs-probe` that needs no GPU: it checks that the documented names resolve, not what they return. `DEBUG_FIELD_SAMPLE=all` checks all 264.

## Known limits

- The `-Werror` sweep covers `src/` only, so a warning introduced in a test file is still caught by nobody until CI compiles it with its per-test macros.
- `test_scene_reachability.sh` proves a scene is invoked, not that it asserts anything.
- `test_public_header_seams.sh` pins a list; it does not judge whether a seam is justified. That stays a review question.
- The four benchmark invariants are still the recorded semantic values. If the mesh pipeline legitimately changes them, the test and the documents move together — which is the point, but it does mean the test is a change detector, not an oracle.

## Related

- [record 24: world type decoupling](24-world-type-decoupling-2026-10-25.md) — the split whose gate was guarding the wrong headers
- [documentation rules](../documentation-rules.md) — the gate that reports these documents stale when the anchors below move
