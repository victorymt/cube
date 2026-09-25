# Flat-world camera rotation baseline

This baseline compares a fixed player position with a camera that stays still
and a camera that turns one degree per frame. Both routes use flat terrain,
first-person view, the same isolated runtime directory, 1280x720, and the
effective render distance reported by the trace (`1`). The routes run 720
playing frames after the local stream is settled. The control still sends
`look delta 0 0` every frame so the debug input path is present in both runs.

```sh
build/normal/voxelcraft --debug-resolution 1280x720 \
  --debug-trace /tmp/flat-static.jsonl --debug-trace-frames \
  --debug-script tests/scenes/flat_static_benchmark.dsl

build/normal/voxelcraft --debug-resolution 1280x720 \
  --debug-trace /tmp/flat-rotation.jsonl --debug-trace-frames \
  --debug-script tests/scenes/flat_rotation_benchmark.dsl
```

The benchmark keeps the player position at `[0.499846, 9.004445, 0.5]` for
all 720 measured samples. The rotation route ends at yaw `12.566313` radians,
which is two complete turns. Both traces report nine candidate chunks and
GPU timing support for every measured sample; rotation changes the visible
section count from 4–9 as the frustum moves.

| metric (720 samples) | static | rotation |
| --- | ---: | ---: |
| update main CPU mean / p95 / max (ms) | 0.566 / 0.577 / 12.485 | 0.561 / 0.537 / 12.517 |
| render main CPU mean / p95 / max (ms) | 1.372 / 1.587 / 1.794 | 1.468 / 1.571 / 1.713 |
| world submission CPU p95 (ms) | 0.086 | 0.083 |
| `EndDrawing` wall mean / p95 / max (ms) | 15.425 / 15.616 / 15.843 | 15.431 / 15.608 / 16.672 |
| GPU world pass mean / p95 / max (ms) | 0.281 / 0.301 / 0.310 | 0.283 / 0.307 / 0.320 |

The three large update spikes occur at frames 240, 480, and 720 in both
routes. Each is an ecology step of about 11.6–12.0 ms; the corresponding
world submission and GPU values stay below 0.4 ms. Camera rotation adds about
0.096 ms of render CPU time on average, while the GPU difference is about
0.002 ms. This points to the periodic ecology step as the visible long-frame
source in this flat scene, with frustum work adding a smaller steady CPU cost.

`timing.end_drawing_wall_ms` includes Raylib's swap/pacing and input-polling
work, so it is useful for detecting display synchronization but is not a pure
present-latency measurement. `render.gpu_world_ms` covers the instrumented
world pass only; it excludes the background, HUD, and other `EndDrawing`
operations. GPU samples carry `gpu_frame` because the nonblocking query can
arrive a few frames after submission.
