# Planet landform quality review

Review date: 2026-10-07. Numerical gates pass. **Visual acceptance remains
blocked at the poles; do not enable Coriolis terrain bias on this evidence.**
This stage adds tests and read-only diagnostics, not a new terrain version.

## Reproduction

```sh
make test-landform-quality
make test-landform-quality-e2e
```

The first target needs no display. The second requires Hyprland/Wayland and
retains `check.dsl`, `game.log`, `trace.jsonl`, twelve PNG/report pairs and
`review.md` in the printed `/tmp/voxelcraft-landform-quality.*` directory.
Reports are checked for matching capture/chunk IDs, a camera outside solid
terrain, ready local streaming, and at least 180 simulation ticks of warmup.
These are capture checks, not an automated judgment of screenshot quality.
The dry controls use Y=220: an initial Y=260 camera made `stream wait` request
unresolved sections above the bounded planetary generation range and time out.
That separate wait-range limitation is not evidence of a landform seam.

## Numerical matrix

These are controlled synthetic `PlanetProfile` fixtures, separate from the
legacy generated profiles used by the graphical debug-surface command.
Rotation is `PlanetProfile.rotationRate`, degrees per game-time unit; the
strategy converts it to physical radians/second. Temperature is 288 K.

| Seed | Radius (km) | Gravity (m/s²) | Rotation | Ocean coverage | River cells | Lake cells | Mouth reaches | Largest extra face-seam elevation jump (blocks) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1234 | 1500 | 3 | 0 | 0.35 | 15489 | 5395 | 391 | 0.680618 |
| 4321 | 5000 | 7 | 180 | 0.60 | 10316 | 1542 | 335 | 0.079575 |
| 991 | 11000 | 18 | -360 | 0.85 | 4163 | 4 | 154 | 0.682877 |

Each fixture checks all drainage cells for acyclic downstream ordering,
non-increasing spill levels, catchment/discharge accumulation and outlet area
conservation. River counts describe low-resolution cells, not independent
named rivers. Rebuilding the same input produces identical cell arrays.

Longitude wraps and discrete north/south pole reflections produce exactly
equal sample fields. All twelve cube edges are sampled at 63 positions on
both sides (756 pairs per fixture), less than .01 block apart. The reported
jump subtracts the integer baseline difference and must stay below one block.
This does not establish analytical continuity or zero sub-block artifacts.
Additional samples check finite values, normalized deposits, and at least one
whole water voxel above every reported wet bed. Physics diagnostics stay
finite, with zero deflection for the non-rotating fixture.

## Visual observations and acceptance limits

The replay captures a mouth and lake for each of seeds 1234, 4321 and 991,
then the date line, +X/+Z cube-face edge, and both poles on seed 991.
`landform find mouth` means a wet river sample on a reach draining directly
to a sea-level cell; it does not promise the exact coastline voxel.

The first captures were too local: paused streaming only prepared a small
window. The replay now explicitly runs 180 normal input frames, waits for
the input to finish, restores the survey camera and waits for its local
sections. Merely issuing `input` and immediately pausing does not warm up
streaming. The final captures preserve the HUD and capture banner.

In the warmed temperate views, the sampled river mouths meet the visible
sea and the sampled lake surfaces meet nearby terrain. The sampled date-line
and cube-face views show no obvious extra vertical terrain wall at their
centers. Distant terrain/water coverage boundaries remain visible; these
images are not a full-basin shoreline or global drainage-quality guarantee.

Both exact-pole-neighborhood views still show a narrow vertical strip rather
than an adequately covered ground surface after warmup. Numeric sample
identity and `stream.surface_ready` both pass, so neither is sufficient for
polar visual acceptance. The replay includes north/south dry-planet controls
with `landform.available == false` to separate this from drainage generation.
Both dry controls reproduce the same narrow strip, confirming that the defect
does not depend on the landform atlas. The final twelve-view replay passed all
DSL/capture checks; its polar screenshots still fail visual acceptance.

The relevant coverage path is `CollectMissingChunkCandidates` in
`src/world/chunks_streaming.c`: it requests a fixed square in map chunk
coordinates. `CurveSurfacePoint` in `src/world/chunks_runtime.c` projects
those vertices onto the sphere. Near a pole, many adjacent longitude columns
cover only a very small physical width. This is a surface coverage/rendering
problem to resolve before adding river-shape physics; changing drainage
parameters or clamping latitude would conceal it and change unrelated terrain.

Next work: make polar streaming/visible coverage consistent with the spherical
projection, then repeat the two polar views and their dry controls. Keep
Homeworld v7/v8 sample fingerprints unchanged. After visual acceptance,
calibrate flow diagnostics before designing a separately versioned Coriolis
bias. The present fixed-runoff diagnostic is not a calibrated migration model.
