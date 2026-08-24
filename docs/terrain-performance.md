# Terrain and mesh performance baseline

The repository benchmark is a repeatable mesh-pipeline baseline, not a
machine-independent frame-time gate. Run it three times on the same host:

```sh
for run in 1 2 3; do
    make -s benchmark-chunks |
        awk -F= '/^(median_total_ms|p95_total_ms|mesh_snapshot_bytes|solid_vertices_per_chunk|solid_vertex_reduction_percent|sync_rebuilds)=/ { print $1 "=" $2 }'
done
```

The reference run recorded on 2026-08-24 used seed `1448040515`, 20 measured
rounds, and 8 chunks per round. The three samples were:

| sample | median ms | p95 ms | snapshot bytes | solid vertices/chunk | reduction | sync rebuilds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 37.376 | 38.437 | 8192 | 2187.8 | 68.5% | 0 |
| 2 | 37.460 | 39.116 | 8192 | 2187.8 | 68.5% | 0 |
| 3 | 37.928 | 40.837 | 8192 | 2187.8 | 68.5% | 0 |

For a hot-path change, preserve these semantic invariants exactly:

- `mesh_snapshot_bytes = 8192`;
- `solid_vertices_per_chunk = 2187.8` and `solid_vertex_reduction_percent = 68.5`;
- `sync_rebuilds = 0`.

Compare elapsed time only against a same-host three-sample run; a change over
roughly five percent should be investigated rather than treated as a portable
failure. This benchmark exercises `BuildChunkMeshData` and related mesh
emitters. It does not replace the planet generation, chunk streaming, or
runtime scheduler tests, which must be run separately.
