# Regional ecology continuity

Regional state is retained across cache eviction and saved in ecology payload
V7. The explicit scheduler prepares the player's 128-cell neighborhood with a
migration halo, independent of render distance and chunk job completion. Its
working set includes hot and cold records. Population, lineage, map, HUD and
flora queries read committed state without admitting or advancing regions.

The application advances ecology, entities, fluid and entity weather/fire/
tornado hazards from the existing fixed simulation clock. It prepares regions
before accepting local simulation or editing. Catch-up runs at most eight
complete regional batches per frame while simulation time stays fixed; no
unprocessed duration is silently discarded. Navigation remains available so
the player can return to an existing region if new admission fails. Pauses,
atlas/map, import/album and landing transitions use the same simulation gate.

Entities render interpolated previous/current poses. Interpolation follows
spherical directions and transforms headings between local tangent frames;
slot reuse, save/load, surface/origin changes and teleports reset it. These poses
are presentation history, do not enter the ENT4 payload, and do not modify
simulation state or consume randomness.

World format V23 adds an ECL1 homeworld daylight anchor before the spherical
trailer (which must remain at EOF). The double anchor is stable between explicit
clock changes, so sampling an earlier time does not drift as frames advance.
V18–V22 saves initialize this missing clock; legacy saves cannot recover an
unsaved historical phase. F9 and DSL loads synchronize both the daylight state
and fixed clock to the restored Space elapsed time. Day-cycle toggle history is
not a complete event log; the latest anchor describes the active clock segment.

## Verification

- Ecology system tests cover eviction, 1089-region checkpoints, V6 compatibility,
  failed allocations, malformed loads, read-only sampling, region-set hash
  collisions and budget/checkpoint partition equivalence.
- Application seam tests hold per-tick player inputs constant and compare home
  and planet runs at 30/60/144 FPS and irregular frame partitions. They cover
  pause gates, time scaling, catch-up and clock-load atomicity. Player movement
  remains sampled by frame, so these are not whole-game deterministic replays.
- Entity replay tests exercise interpolated poses at the longitude seam and
  poles, lifecycle resets and unchanged simulation snapshots.
- The graphics DSL regression pauses, reads ecology, saves, reloads and compares
  regional state through the actual executable. Its first run caught ECL1 being
  placed after the EOF-checked spherical trailer; moving it before the trailer
  fixed the round trip. Failure artifacts are retained by the harness.

The fluid solver retains its existing work budget and FLD2 checkpoint limits.
Fixed application scheduling alone does not persist its active work queue or
make asynchronous chunk availability independent of rendering.
