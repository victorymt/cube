# G1/G2/G3: Persistent volcano lifecycle, lava physics and water contact

G1 adds deterministic vent candidates and a saved lifecycle preview. G2 adds
finite lava inventory, a 10 Hz cell runtime, temperature/enthalpy cooling,
partial-height collision overlays and full-cell basalt conversion. Water contact is
now a fixed-step G3 interaction. P0 adds finite ash layers and material-aware
cooling products; heat damage and ecological restoration remain later work.

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
solid overlays over air; full cooled cells commit to their selected rock. The vent's orange color is a
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
its selected rock through an environment mutation.

World save V27 appends `VOLCANO1`, `LAVA0003` and `ASH00001` after the ecology clock and before the spherical
trailer. Every scalar uses explicit little-endian encoding (IEEE-754 binary64
for real values); struct padding and pointers are never written. Records,
admission state, discovery timer, rejection count and fractional tick remainder
are saved. Load validates a temporary bounded array, then commits it. The outer
world transaction includes these payloads in its checkpoint/rollback. V26 reads
`LAVA0002` with zero material markers. V25 reads
legacy `LAVA0001` with empty reaction counters/credits. V24 keeps
its volcano records with empty lava state; V18–V23 reset both. A new world resets
all three runtimes; switching surfaces does not discard them. Pre-V27 saves reset ash.

The lava runtime has at most 2,048 hot cells and 8,192 total records. Cold partial
deposits free hot capacity while retaining storage, geometry and collision. New
cells are rejected at capacity and rejected source volume remains in the vent.
The G2 reservoir is finite (256 blocks per vent); there is no automatic refill.
World callbacks confine each active domain to +/-96 map blocks and +/-96 Y from
its vent. Unknown terrain sections and domain borders are impermeable. Cells
outside admitted domains remain parked with their enthalpy unchanged.

World extension hooks expose partial blocks, solid collision heights and liquid
surface heights, and reject ordinary edits that would delete a partial record
or turn it into a whole inventory block. Full cooled rock is a normal editable
block. Block collection credits inventory only after a successful world mutation,
so a rejected partial-deposit edit cannot duplicate items. Fractional excavation/
displacement is deferred. The existing water solver checks overlays before
transfer/deposition; no volume overlap is allowed. Loaded fluid state overrides
AIR/WATER terrain edit baselines so excavated cells can fill and placed water
can evaporate; solid terrain edits retain priority. Legacy static lava fire ignition reads base
voxels so it does not prematurely turn the new overlay into a heat hazard.

Chunk mesh workers and snapshot generation read base voxels only; they never
read the main-thread lava hash or hide a neighboring full face behind a partial
overlay. Runtime lava meshes are drawn separately on the main thread. They use
the same camera-relative section transform as terrain and shadows, including
spherical rotation and vertical displacement, with cell positions in section
coordinates. Entity
path selection still treats an occupied overlay conservatively as lava/rock;
player/camera collision and immersion use actual partial heights.
Selection rays intersect the selected solid/liquid interval of an overlay and
return that surface's face normal. Camera occlusion uses only its solid height,
including a solid base under liquid; empty space above a deposit stays clear.

## Verification

- `test_volcano`: lifecycle, forced restart/stop, canonical aliases, face-edge
  candidates, pressure-gated natural events, parked/unloaded/body transitions,
  frame partitions, query purity, storage/active caps, fractional checkpoint
  continuation, every byte truncation and oversized-count rejection.
- `test_lava`: thermal enthalpy, flow order, unknown boundaries, capacity,
  cooling, partial overlays, basalt conversion, inventory debit, conservation,
  checkpoint continuation and corrupt/truncated payload rejection; finite/shared
  water, ocean accounting, contact gaps, failed commits and reaction replay.
- `test_render_volcano`: solid/liquid layer geometry aligned with terrain at
  distance, high latitude, longitude wrap, rebased origins and negative sections;
  bounded steam uses the same transform and no shared random generator.
- `test_block_collection`: failed mutations yield no inventory items; successful
  collection commits the world edit before inventory, and a full inventory
  prevents removal.
- `test_interaction_raycast`: rays above, inside and into the top of partial
  deposits; solid/liquid masks, mixed layers, reach limits and face normals.
- `test_save_format` and `test_game_save_spherical`: V27 flags/writer, new
  payload routing, old-version reset without consuming trailer bytes, corrupt
  payload preserving live records.
- `test_debug_control`: command syntax, parameter limits, hex identity parsing.
- `test-volcano-e2e`: real Wayland force/step/save/load/parking/stop/restart route,
  partial-height collision, closed-basin basalt conversion and another save/load,
  plus sealed large/small water basins, mid-reaction replay, unloading and steam,
  with retained screenshots and traces, using the existing project DSL.

Water reaction callbacks are supplied by `app/game_geology`, outside the water
solver. Six face credits accrue at 128 × contact area × temperature excess per
second. One 1/255-block water parcel costs 4096 lava enthalpy units (sensible plus
latent); water is assumed to enter at 293 K. Credits are not heat: failed water
compare-and-consume cannot debit lava enthalpy. Water work, heat debit and steam
accounting commit on the main thread before ambient cooling and solidification.
Procedural ocean cells remain reservoirs and report their external replenishment
separately from finite-water loss. Unknown faces park credits, known dry or separated
faces clear them. `LAVA0003` saves credits, counters and material markers, validates energy and mass
relations, and clears transient steam on load. Rendering caps steam at 96 puffs
and never changes reactions. Full-runtime water flow still has a work budget;
cross-system deterministic tests explicitly control water ticks.

G3 water contact is a bounded six-face parcel model. It does not provide water
temperature transport, steam pressure, explosions, heat damage or ecological restoration;
those require separate models and integration.

## P0 ash and cooling products

`world/ash_runtime` owns at most 2048 cells, with 1024 volume units per voxel.
`app/game_geology` supplies loaded-air boundaries, deterministic wind-directed
fallout and rain exposure at the frozen geology sample time. Volcanoes debit
only accepted ash supply from their finite reservoir after lava supply. The
10 Hz step sorts by body/Y/Z/X, applies fractional rain erosion and moves ash
at most one voxel down, preserving `supplied = volume + washed`. Rain exports
sediment rather than synthesizing mud blocks. Unknown terrain, other bodies and
cells outside the observer's ±96-block neighborhood park. The canopy probe is
limited to 12 blocks; it is not a global sky-visibility solver. Full and partial
ash remain overlay records so both continue eroding. Main-thread drawing uses
terrain section transforms, and the existing overlay hooks expose exact heights.

Lava cells now carry volume-weighted silica, vesicle and quench-history markers.
Natural source archetypes hash the complete stable identity and seed; debug-forced
vents preserve dense mafic supply. Flow conserves composition markers; committed
water cooling adds the amount newly frozen to a bounded quench marker. This
homogeneous model retains that history through reheating/flow. It does not model
bubble expansion, pressure, mineral kinetics or variable density. Gas-rich mafic
and silicic cells yield scoria and pumice. Dense mafic cells yield basalt; dense
silicic cells yield obsidian at a quench fraction of at least 0.25, otherwise
rhyolite. Silica/vesicle archetype cutoffs are 0.5. The same classification drives
partial solids and the callback that writes full cooled blocks.

`LAVA0003` appends three binary64 marker volumes to each V2 cell record; V1/V2
load them as zero (legacy basalt). `ASH00001` stores five u64 header values
(count, supplied, washed, rejected, ticks) and six u64 values per record
(body, x, y, z, volume, binary64 erosion credit). Both validate bounded finite
state before committing. The world transaction checkpoints both payloads.

`test_ash` covers rain/gravity, canonical aliases, capacity, inventory debit,
parking, fractional continuation and every byte truncation. `test_lava` adds
material transport, all five real cooling outcomes, V1/V2 migration and corrupt
marker rejection. Two extra DSL scenes cover actual ash selection/collision,
roof protection, falling, persistence and final rock blocks.
