# Architecture: Chunk Performance and Runtime Verification

The performance boundary remains the existing chunk pipeline: canonical chunk
indexing prevents duplicate surface work, generation and mesh jobs are ordered
by priority, and worker-local scratch buffers keep temporary mesh memory out of
shared ownership. These changes are isolated from block, weather, fluid,
ecology, hazard, and save semantics.

The implementation is represented by three focused commits:

- `28362db perf: add canonical chunk index`
- `bc71320 perf: schedule generation and mesh work with priority queues`
- `813d0e9 perf: reuse worker-local mesh scratch buffers`

The benchmark uses a fixed seed and eight chunk coordinates, reports median and
P95 mesh time, snapshot bytes, vertex reduction, and synchronous rebuilds. It
is a streaming/meshing gate rather than a full locomotion workload. UI text
buffering and render batching were not added because the existing measurements
do not establish them as the dominant cost.

Three local benchmark samples on 2026-08-30 reported medians of 36.862 ms,
38.036 ms, and 45.540 ms; each retained `mesh_snapshot_bytes=8192` and
`sync_rebuilds=0`. The full C manifest passed 86 tests after the later
simulation stages. The dedicated CI performance job runs the same benchmark;
the graphics job uses a real Hyprland runner and the project's DSL E2E route.

Known limits: benchmark variance is visible on a loaded desktop, and the full
graphics E2E route timed out in its final long polar-input scenario in the
current session even though the shorter graphics/debug route passed. This is a
tracked reliability issue, not hidden by widening a benchmark threshold.
