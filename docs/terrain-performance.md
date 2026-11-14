# Terrain and mesh performance baseline

This is a **dated baseline record**, not a portable frame-time gate. The recipe
below is the only part of this page that stays current; every number is a
snapshot of the host it ran on, and elapsed times are only comparable within one
host and one session.

## Recipe

```sh
for run in 1 2 3; do
    make -s benchmark-chunks |
        awk -F= '/^(median_total_ms|p95_total_ms|mesh_snapshot_bytes|solid_vertices_per_chunk|solid_vertex_reduction_percent|sync_rebuilds)=/ { print $1 "=" $2 }'
done
```

The benchmark prints its own `seed`, `rounds`, and `chunks_per_round`, so read
those instead of trusting the numbers below. As of 2026-10-24 it reports
`seed=1448040515`, `rounds=20`, `chunks_per_round=8`.

On 2026-11-13 the same recipe reported `median_total_ms=53.545`,
`p95_total_ms=61.116`, `solid_vertices_per_chunk=2187.8`, and
`sync_rebuilds=0` on the then-current host.

After the P3 actor-motion move, the same recipe reported
`median_total_ms=53.287`, `p95_total_ms=53.845`,
`solid_vertices_per_chunk=2187.8`, and `sync_rebuilds=0`.

After the P4 Player state/fixed-step work, the same recipe reported
`median_total_ms=52.787`, `p95_total_ms=53.726`,
`solid_vertices_per_chunk=2187.8`, and `sync_rebuilds=0`.

After adding Player state to the integration replay checkpoint, the same
recipe reported `median_total_ms=53.143`, `p95_total_ms=53.567`,
`solid_vertices_per_chunk=2187.8`, and `sync_rebuilds=0`.

After moving environment sampling into the world query layer, the same recipe
reported `median_total_ms=53.372`, `p95_total_ms=53.678`,
`solid_vertices_per_chunk=2187.8`, and `sync_rebuilds=0`.

After adding the P6 rule/frame foundation, the same recipe reported
`median_total_ms=52.669`, `p95_total_ms=53.218`,
`solid_vertices_per_chunk=2187.8`, and `sync_rebuilds=0`.

After connecting gravity rules to environment samples, the same recipe reported
`median_total_ms=53.798`, `p95_total_ms=54.499`,
`solid_vertices_per_chunk=2187.8`, and `sync_rebuilds=0`.

After adding cross-frame point APIs, the same recipe reported
`median_total_ms=53.306`, `p95_total_ms=53.565`,
`solid_vertices_per_chunk=2187.8`, and `sync_rebuilds=0`.

After connecting runtime rules to Player environment consumption, the same
recipe reported `median_total_ms=54.232`, `p95_total_ms=55.710`,
`solid_vertices_per_chunk=2187.8`, and `sync_rebuilds=0`. The test manifest and
runtime rule path changed; the chunk mesh benchmark closure and semantic
invariants did not.

After adopting the save-local frame boundary, the test manifest added the world
frame source to the save test closure. The same recipe reported
`median_total_ms=53.687`, `p95_total_ms=55.387`,
`solid_vertices_per_chunk=2187.8`, and `sync_rebuilds=0`; the benchmark source
closure and invariants remain unchanged.

After extracting ordinary ground locomotion from the Player controller, the
test manifest gained a focused actor-locomotion closure. The same recipe
reported `median_total_ms=53.526`, `p95_total_ms=54.383`,
`solid_vertices_per_chunk=2187.8`, and `sync_rebuilds=0`; the benchmark source
closure and invariants remain unchanged.

## Semantic invariants

These are behaviour, not performance, and must hold after any hot-path change:

- `mesh_snapshot_bytes = 8192`;
- `solid_vertices_per_chunk = 2187.8`, `solid_vertex_reduction_percent = 68.5`;
- `sync_rebuilds = 0`.

`tests/benchmark_chunks.c` also prints `legacy_solid_vertices_per_chunk` (6952.5
at the same baseline) so the reduction is derivable rather than asserted.

## Samples

Two sample sets exist. They are not comparable to each other: different hosts,
different months, and the second ran after several terrain and volcano changes.

| Recorded | sample medians (ms) | snapshot bytes | solid vertices/chunk | reduction | sync rebuilds |
| --- | --- | ---: | ---: | ---: | ---: |
| 2026-08-24 | 37.376 / 37.460 / 37.928 | 8192 | 2187.8 | 68.5% | 0 |
| 2026-10-24 | 53.406 / 53.659 / 53.445 | 8192 | 2187.8 | 68.5% | 0 |

The 2026-08-24 p95 samples were 38.437, 39.116, and 40.837 ms; the 2026-10-24
p95 sample measured 57.414 ms. The median moved by about 42% between the two
sets. That is not evidence of a regression: the mesh emitter still produces the
same bytes and vertices, and the intervening commits changed terrain sampling
and volcano layering, so the comparison is a different workload.

Compare elapsed time only against a same-host three-sample run taken now; a
change over roughly five percent is worth investigating rather than treating as
a portable failure.

## Scope

The benchmark exercises `BuildChunkMeshData` and the related mesh emitters. It
does not replace the planet generation, chunk streaming, runtime scheduler,
or input-event tests, which run separately. `mk/tests.mk` registers those test
closures alongside other unit tests; adding them does not change the benchmark's
source closure or semantic invariants. `make benchmark-chunks` is the CI
performance job.

It could not run at all from the volcano-mountain integration until 2026-10-24:
`mk/tests.mk` linked the benchmark without `PLANET_SURFACE_SRC`, so the target
failed to link. Anyone who "verified" performance in that window was measuring
nothing. See
[record 09](architecture/09-performance-optimization.md#architecture-chunk-performance-and-runtime-verification)
for the boundary that owns that gate.

M1 does not change the measured mesh path or benchmark closure. The new
`test_surface_contract` joins the normal `make test` sweep; the benchmark
invariants and its source closure remain unchanged.

M3 adds V30 save/cell reload tests to `mk/tests.mk` but does not alter the chunk
mesh benchmark closure. Re-run `make -s benchmark-chunks` only when changing mesh
generation or its source closure, not for save-format codec tests.

The 2026-11-11 cube-ray test closure adds `surface_geometry.c` to one unit-test
entry only. Re-running `make -s benchmark-chunks` after that manifest change
reported the unchanged invariant set (`mesh_snapshot_bytes=8192`,
`solid_vertices_per_chunk=2187.8`, `solid_vertex_reduction_percent=68.5`,
`sync_rebuilds=0`) with a 54.191 ms median and 55.170 ms p95 on this host.

The 2026-11-12 cube-native tree closure adds generation and E2E test entries but
does not change the benchmark's mesh source closure. Re-running
`make -s benchmark-chunks` reported the same invariant set with a 53.426 ms
median and 54.629 ms p95; fleet variants were 54.629 ms and 55.143 ms, and the
greedy CPU ratio remained 96.9%.
