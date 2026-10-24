# Coupling review (2026-10-24)

- Baseline: `fa11949`
- Method: measured include graph per component, header fan-in, real rebuild radius from `build/normal/obj/**/*.d`, and mutation tests against `tests/test_architecture.sh`
- Nature: **review only, no code changes**. Every number below came from a command in this session.

## 0. Verdict

Coupling is **acceptable for a project this size, with one structural weak point**. The component split holds, the hard layering invariants are real, and the boundary gate genuinely rejects violations. Two of the four findings in the [2026-10-23 review](20-health-review-2026-10-23.md) are fixed.

The weak point is not a layering violation: `world` is depended on directly by every other component, and four of its headers each touch more than a third of all translation units. Nothing is *wrong*; the cost is that `world` changes are expensive and `world` has become the de facto shared kernel.

## 1. Dependency matrix (measured at this commit)

Rows depend on columns, counted as `#include` statements.

```text
             core world space ecolo gamep prese   app
core            0     0     0     0     0     0     0
world          11     0     9     0     0     0     0
space           8     9     0     1     1     0     0
ecology         2    38     6     0     1     0     0
gameplay        7    40    11     0     0     0     0
presentation   10    59    45    20    22     0     0
app            72   204    47    24    49    52     0
```

Holds from the previous review: `core` still has zero upward dependencies, `app` is still only referenced from `app/`, and `presentation` still never reaches `app`.

Changed: `world → ecology` is now **0** (was 7) and `gameplay → presentation` is now **0** (was 1). Both were fixed by `a730e7e`, which moved `flora_taxa` into `world` and `ui_types.h` into `gameplay`.

`app → core` rose 69 → 72 because `app` now owns the debug command surface through `core/debug_control.h` (19 includes) and `core/debug_dsl.h` (14). That is the composition root doing its job.

## 2. Remaining cycles

Three mutual edges survive. All are **value-type cycles**, not behavioural ones:

| Cycle | Counts | Shape |
| --- | --- | --- |
| `world ↔ space` | 9 vs 9 | `world/terrain.h` embeds `PlanetSurfaceSample`; `space/planet_surface.h` embeds `VolcanoTerrainSample` |
| `gameplay ↔ space` | 11 vs 1 | `gameplay` reads space state directly; `space` reaches `gameplay` only through `space_dependencies.h` |
| `ecology ↔ space` | 6 vs 1 | same aggregator pattern, reverse direction |

The `world ↔ space` pair is the only one that is genuinely load-bearing: `PlanetSurfaceSample` contains a `VolcanoTerrainSample` by value, and `terrain.h` needs both. Breaking it would mean splitting the sample into a neutral value header, which is a real refactor rather than a move.

`space` reaches `ecology` and `gameplay` only through the single aggregator `space_dependencies.h`, and `tests/test_architecture.sh` enforces that. That is a deliberate, checked sink rather than scattered coupling.

## 3. The real weak point: `world` as shared kernel

`world` has 66 public headers; 48 are included from outside it. Four headers each touch a large share of the 235 translation units:

| Header | Translation units rebuilt | Share |
| --- | ---: | ---: |
| `world/world_environment.h` | 108 | 45% |
| `world/terrain.h` | 90 | 38% |
| `world/world.h` | 87 | 37% |
| `world/chunks.h` | 82 | 34% |

So a change to `world_environment.h` forces a rebuild of nearly half the codebase, and — more importantly — those four headers are read by six of the seven components. `world` is not a module any more; it is the platform.

Header fan-in confirms it: `world/world.h` (67 includes, 6 components), `world/terrain.h` (52, 6), `world/world_environment.h` (50, 6), `world/chunks.h` (46, 6).

`world/world.h` compounds this. It exposes 58 functions spanning five unrelated responsibilities: world lifecycle (`WorldGetSeed`, `WorldReset`), block taxonomy predicates (`IsWaterBlock`, `IsEcologyBlock`, …), torch lighting (`TorchLightAdd`, `CollectNearbyTorchLights`), edit bookkeeping (`WorldGetEditRevision`, undo groups), and block read/write (`GetBlockAt`, `SetBlock`). Any of the six consumers that wants one predicate drags in all five groups.

Twelve headers under `src/world/` are included by no other component, but eleven of those twelve are used by tests, so most are legitimate test seams rather than leaks. `world/chunks_dependencies.h` is consumed by exactly one file, `world/chunks_internal.h`, and by nothing else — an intra-component aggregator with a single consumer, not a cross-component leak. (This paragraph was corrected on 2026-10-24: the original text called that header dead surface, which the include graph contradicts.)

## 4. `app` internal coupling

`GameRuntime` still carries 137 top-level fields in a 212-line header, and 24 of the 43 `app` source files include it. It never leaks outside `app/`, so it is not an architectural violation — but it is the single point every `app` feature must edit, which is why `app` commits tend to be wide (`git log --stat` shows most of them touching more than ten files, far more than the `world` or `docs` commits).

## 5. Are the guards real?

Checked by mutation rather than by reading the rules:

| Injected violation | Result |
| --- | --- |
| `gameplay/interaction.c` includes `presentation/render.h` | `architecture check failed: gameplay must not depend on presentation` |
| `space/space_physics.c` includes `ecology/ecology.h` directly | `architecture check failed: space must consume ecology only through its dependency aggregator` |

Both reverted cleanly. The rules are enforced, not decorative. 127 `fail` assertions run in the script.

Independently verified invariants at this commit: zero `core → higher` includes, zero non-`app → app` includes, zero `presentation → app` includes.

## 6. What I would fix, in order

1. **Split `world/world.h` by responsibility.** The five groups above are separable without touching behaviour, and every split shrinks the include set for the other five components. Highest value per risk.
2. **Break the `world ↔ space` value cycle** by moving `PlanetSurfaceSample`/`VolcanoTerrainSample` into a neutral value header. Real work; do it when the cube-grid conversion touches those types anyway, so the churn is paid once.
3. **Rename `world/chunks_dependencies.h` to `_internal.h`** — its only consumer is `world/chunks_internal.h`, so it should not read like a public header. (This item originally said nothing consumes it; that was wrong.)
4. **Leave `GameRuntime` alone** until `app` grows a second concern. 137 flat fields are ugly, not harmful.

Not worth doing: chasing the `space ↔ ecology` / `space ↔ gameplay` cycles through the aggregator. They are one checked edge each and removing them would scatter the dependency instead of concentrating it.

## 7. Reproducing

```sh
python3 scripts/coupling-report.py            # matrix, cycles, fan-in

# rebuild radius: how many translation units a header pulls in
grep -l "src/world/world_environment.h" build/normal/obj/*/*.d | wc -l

# guard effectiveness: inject a violation, confirm the gate rejects it
make test-architecture
```

The same script is what produced sections 1 and 3. Cycle counts in section 2 are include statements in each direction (`space ↔ world` is 9 vs 9, not the header-file counts quoted from the earlier review).
