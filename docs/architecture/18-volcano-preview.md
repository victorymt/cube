# G1/G2: Persistent volcano lifecycle and lava physics

G1 adds deterministic vent candidates and a saved lifecycle preview. G2 adds
finite lava inventory, a 10 Hz cell runtime, temperature/enthalpy cooling,
partial-height collision overlays and full-cell basalt conversion. Water
reactions, heat damage, ash deposition and ecological restoration remain G3/G4.

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
cells when discovering new candidates. Homeworld discovery is disabled,
but the DSL can create a fixed vent there.

Admission uses distance then ID; admitted records persist within a 224-block
hysteresis radius and waiting records enter within 160 blocks. Every admitted
vent receives all 2 Hz steps, independent of retained-record occupancy. The
0.5-second maximum update input matches the fixed-step contract; invalid or
large direct calls do not silently fast-forward. Unloaded vents, distant vents
and other bodies park without losing fractional time or event history. A
blocked outlet remains admitted so existing lava can cool; new supply is rejected.
Recovery waits only for nearby hot cells whose boundary is open in the current
lava context. Parked cells outside the admitted domains or in unloaded sections
retain their heat without preventing another vent from recovering.

`presentation/render_volcano` reads state copies and draws a small vent, ash
column and sparks with bounded geometry. It writes no simulation state and
uses no random generator or particle queue. Lava cells are drawn as liquid and
solid overlays over air; full cooled cells commit to basalt. The vent's orange color is a
visual cue, not a dynamic light source. G2 deposits in air only, preserving
existing blocks, and does not expose a heat damage model. The independent
activity/damage policy belongs to G3 before destructive interactions are enabled.

## State and persistence

Phases are dormant, unrest, erupting, waning, cooling and recovery. Natural
events require pressure and inventory; forced vents can be restarted from
dormant/recovery but cannot reset an already running event. Each restart
increments an event sequence. Stop enters cooling, preserving history.
G2 debits the reservoir only for accepted 1/1024-block deposits. Eruption
discharges finite volume; flow samples the previous state, goes downward before
lateral equalization, and treats unknown or blocked cells as closed. Destination
capacity is resolved proportionally, making the result independent of iteration
order. Cooling uses normalized enthalpy and a latent interval at 1100 K; partial
solid overlays retain their collision height, while a full cold cell becomes
basalt through an environment mutation.

World save V25 appends `VOLCANO1` and `LAVA0001` after the ecology clock and before the spherical
trailer. Every scalar uses explicit little-endian encoding (IEEE-754 binary64
for real values); struct padding and pointers are never written. Records,
admission state, discovery timer, rejection count and fractional tick remainder
are saved. Load validates a temporary bounded array, then commits it. The outer
world transaction includes these payloads in its checkpoint/rollback. V24 keeps
its volcano records with empty lava state; V18–V23 reset both. A new world resets
both; switching surfaces does not discard them.

The lava runtime has at most 2,048 hot cells and 8,192 total records. Cold partial
deposits free hot capacity while retaining storage, geometry and collision. New
cells are rejected at capacity and rejected source volume remains in the vent.
The G2 reservoir is finite (256 blocks per vent); there is no automatic refill.
World callbacks confine each active domain to +/-96 map blocks and +/-96 Y from
its vent. Unknown terrain sections and domain borders are impermeable. Cells
outside admitted domains remain parked with their enthalpy unchanged.

World extension hooks expose partial blocks, solid collision heights and liquid
surface heights, and reject ordinary edits that would delete a partial record
or turn it into a whole inventory block. Full cooled basalt is a normal editable
block. Block collection credits inventory only after a successful world mutation,
so a rejected partial-deposit edit cannot duplicate items. Fractional excavation/
displacement is deferred to G3. The existing water
solver checks the overlays before transfer/deposition; no water/lava reaction
or volume overlap is allowed. Legacy static lava fire ignition reads base
voxels so it does not prematurely turn the new overlay into a heat hazard.

Chunk mesh workers and snapshot generation read base voxels only; they never
read the main-thread lava hash or hide a neighboring full face behind a partial
overlay. Runtime lava meshes are drawn separately on the main thread. They use
the same camera-relative section transform as terrain and shadows, including
spherical rotation and vertical displacement, with cell positions in section
coordinates. Entity
path selection still treats an occupied overlay conservatively as lava/rock;
player/camera collision and immersion use actual partial heights.

## Verification

- `test_volcano`: lifecycle, forced restart/stop, canonical aliases, face-edge
  candidates, pressure-gated natural events, parked/unloaded/body transitions,
  frame partitions, query purity, storage/active caps, fractional checkpoint
  continuation, every byte truncation and oversized-count rejection.
- `test_lava`: thermal enthalpy, flow order, unknown boundaries, capacity,
  cooling, partial overlays, basalt conversion, inventory debit, conservation,
  checkpoint continuation and corrupt/truncated payload rejection.
- `test_render_volcano`: solid/liquid layer geometry aligned with terrain at
  distance, high latitude, longitude wrap, rebased origins and negative sections.
- `test_block_collection`: failed mutations yield no inventory items; successful
  collection commits the world edit before inventory, and a full inventory
  prevents removal.
- `test_save_format` and `test_game_save_spherical`: V25 flags/writer, new
  payload routing, old-version reset without consuming trailer bytes, corrupt
  payload preserving live records.
- `test_debug_control`: command syntax, parameter limits, hex identity parsing.
- `test-volcano-e2e`: real Wayland force/step/save/load/parking/stop/restart route,
  partial-height collision, closed-basin basalt conversion and another save/load,
  with three retained screenshots and trace, using the existing project DSL.

This stage is not evidence of water reactions, heat damage or ecological
restoration. Those require their own staged models, integration and tests.
