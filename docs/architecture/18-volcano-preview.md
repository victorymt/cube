# G1: Persistent volcano lifecycle preview

This stage adds deterministic vent candidates and a saved lifecycle preview.
It does not release lava or ash material, change blocks, implement heat damage,
or connect cooling to a thermal solver. `reservoir` stays constant and debug
replies declare `preview=1`. The recovery phase is an event cooldown, not yet
ecological restoration. G2/G3 remain necessary for a playable eruption.

## Ownership and identity

`world/volcano_model` defines a stable 64-bit ID from body, generator version,
cube face and face coordinates. Natural candidates use a fixed 128-cell face
grid and compare potential against eight adjacent candidates, including face
crossings. A face cell with no exact integer map representative retains its
face identity and uses a nearby canonical map column for placement. Forced
vents use the exact canonical map location and share longitude/pole aliases.
Potential and terrain height come through callbacks; the pure model does not
query the game world or mutate terrain.

`world/volcano_runtime` owns 64 retained records and at most two admitted vents.
`app/game_geology` supplies body, seed, canonical observer coordinates and
readiness callbacks. Natural discovery runs every two simulation seconds only
on eligible dry planetary volcanic terrain. It rejects edited vent/support
cells; G1 itself never changes those blocks. Homeworld discovery is disabled,
but the DSL can create a fixed preview there.

Admission uses distance then ID; admitted records persist within a 224-block
hysteresis radius and waiting records enter within 160 blocks. Every admitted
vent receives all 2 Hz steps, independent of retained-record occupancy. The
0.5-second maximum update input matches the fixed-step contract; invalid or
large direct calls do not silently fast-forward. Unloaded/blocked vents, distant
vents and other bodies park without losing fractional time or event history.

`presentation/render_volcano` reads state copies and draws a small vent, ash
column and sparks with bounded geometry. It writes no simulation state and
uses no random generator or particle queue. The vent's orange color is a
visual cue, not a dynamic light source. G1 has no damage setting because it
has no damaging behavior; the independent activity/damage policy belongs to
the G3 integration before physical effects are enabled.

## State and persistence

Phases are dormant, unrest, erupting, waning, cooling and recovery. Natural
events require pressure and inventory; forced vents can be restarted from
dormant/recovery but cannot reset an already running event. Each restart
increments an event sequence. Stop enters cooling, preserving history.
In G1 only, eruption and cooling durations drive a preview with zero discharge.

World save V24 appends `VOLCANO1` after the ecology clock and before the spherical
trailer. Every scalar uses explicit little-endian encoding (IEEE-754 binary64
for real values); struct padding and pointers are never written. Records,
admission state, discovery timer, rejection count and fractional tick remainder
are saved. Load validates a temporary bounded array, then commits it. The outer
world transaction includes this payload in its checkpoint/rollback. V18–V23
load with an empty volcano runtime. A new world resets it; switching surfaces
does not discard it.

## Verification

- `test_volcano`: lifecycle, forced restart/stop, canonical aliases, face-edge
  candidates, pressure-gated natural events, parked/unloaded/body transitions,
  frame partitions, query purity, storage/active caps, fractional checkpoint
  continuation, every byte truncation and oversized-count rejection.
- `test_save_format` and `test_game_save_spherical`: V24 flags/writer, new
  payload routing, old-version reset without consuming trailer bytes, corrupt
  payload preserving live records.
- `test_debug_control`: command syntax, parameter limits, hex identity parsing.
- `test-volcano-e2e`: real Wayland force/step/save/load/parking/stop/restart route
  with a retained screenshot and trace, using the existing project DSL.

This preview is not evidence of lava conservation, water reactions or ecological
restoration. Those require their own staged models, integration and tests.
