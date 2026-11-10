# Homeworld cube surface matrix (2026-11-10)

This record covers the M5.3 visual matrix on the Homeworld `cube-grid/1`
backend. It proves deterministic scene coverage and evidence plumbing; it does
not replace the frame-time and memory budgets in M5.4.

## Recipe

```sh
make test-cube-surface-matrix-e2e
```

The test reads `tests/cube_surface_matrix.cases`, generates one DSL script and
runs it under Hyprland at 1280x720. It requires exactly 31 cases: 6 face centers,
12 unique cube edges, 8 corners, both geographic poles, one high-altitude view
and render distances 4, 8 and 12. Every case must produce a screenshot, a full
capture report and a row in `matrix-summary.tsv`.

The route uses `surface address`, so face/edge/corner identity comes from
integer cube coordinates rather than geographic round trips. `fly on` holds the
chosen altitude without opening the pause menu. Each case waits for local
terrain settlement, at least one visible and drawn main-pass section, zero
visible target-LOD misses, and a 30-frame stability window before capture.

## Pass conditions

- Admission is active and not saturated without `requested == admitted`.
- Saturation is recorded explicitly with `requested > admitted`; it is never
  treated as complete visual coverage.
- The render observation sequence equals the stream observation sequence.
- Main-pass candidate, visible and drawn section counts are monotonic, with at
  least one solid draw and triangle submitted in every scene.
- The capture report contains zero visible target-LOD misses.
- Non-saturated face centers, poles, distance cases and high-altitude views are
  checked visually for central terrain gaps; edge/corner captures are checked
  for seam gaps and overlaps. Saturated edge/corner scenes are reported as
  partial-by-policy rather than silently expanded.

## Results

The final run passed 31/31 cases:

- 11 cases were unsaturated with `requested == admitted`.
- 20 edge/corner cases were explicitly saturated with `requested > admitted`
  and the nearest-first 625-column admission cap.
- Every case had local terrain ready, at least one visible/drawn main-pass
  section, a nonzero solid draw and triangle count, and zero visible target-LOD
  misses in its final capture report.
- Generation queue peak was 62 and mesh queue peak was 8 across the matrix.
- Both poles now render above the sea surface with stable target-LOD evidence;
  no narrow legacy-style polar strip appeared.

The 31-row metric table is
[`cube-surface-matrix-2026-11-10.tsv`](cube-surface-matrix-2026-11-10.tsv).
The full run artifacts, including 31 PNGs and 31 key/value reports, were
generated under `/tmp/voxelcraft-cube-matrix.yVHZbE/`; its
`matrix-summary.tsv` is preserved here.

## Limitation found during M5.3

Map-space CGA cannot represent every cube seam or polar chunk as one canonical
map rectangle. Those chunks now keep cube base terrain and omit CGA structures;
representable chunks still generate structures normally. This removes the
previous whole-column generation failure at seams without claiming structure
parity there.
