# Architecture: Off-screen Nutrient Ecology

`ecology_nutrient_model` is the first conservation boundary for ecological
cells that are not currently loaded as voxel chunks. A cell owns mineral and
soil-organic nitrogen/phosphorus, plant and litter nutrient pools, biomass,
seed bank, and mean plant age. `EcologyNutrientStep` advances a finite number
of days in a fixed order: litter decomposition and mineralization, plant
turnover, resource-limited growth, seed production/germination, age advance,
and rainfall-driven leaching.

Plant growth is limited by both nitrogen and phosphorus. Competition scales
growth but cannot create nutrients. Decomposition and turnover transfer the
same nutrient mass between pools; only explicit leaching exports mass. The
returned flux record is suitable for coupling to the existing regional
population model and for deterministic replay diagnostics.

The module is intentionally independent of rendering. Regional population
records now own one adapter state: normalized flora density is mapped to a
fixed biomass scale, climate suitability and soil moisture drive growth, and
the normalized precipitation rate is converted to a bounded millimetre rate
for leaching. Nutrient-supported plant biomass is blended back into the
regional flora density with a limited weight so the existing population model
remains the primary carrying-capacity signal. Disturbance transfers stressed
plant biomass into litter before the nutrient step.

The additional pools were introduced in population state version 6. Version 7
now persists both the 1024-slot hot cache and evicted regional records,
ordered by surface and canonical region coordinates. The checkpoint retains hot
residency; the scheduled simulation set is independent of that cache layout.
Versions 1 through 6 remain loadable; older records lazily initialize their nutrient
state from the saved flora density on the next scheduled population step.
The cold store has a total-record limit of 65536; allocation or admission
failure preserves existing history and reports a failed-admission counter rather
than replacing a record. `PlanetEcologyGetStorageStats` exposes occupancy and
allocated memory. Cold records are included in the next atomic world save, not
written to disk on each eviction. Returning records are restored before catch-up
and gameplay events; migration only joins records on the same simulation step.
Migration now exposes both net and gross directional flora/fauna fluxes.
The population adapter uses the gross fauna directions to breed a deterministic
sample of matching neighboring genomes, raising `geneFlow`, resetting the
receiver's isolation counter, and feeding the resulting founder seed into the
next regional generation. Active cache records are ordered by canonical region
coordinates before edges are applied; each edge also snapshots both parents so
opposite flows cannot depend on mutation order. This keeps replay independent
of LRU slot assignment while remaining a bounded regional approximation: it
does not move individual nutrient pools or persist a complete genome graph.

When genetic variance stays high while gene flow is low, regional evolution now
creates a real species branch in the first available lineage slot. The parent
keeps its lineage/species identity and 75% of its density; a deterministic
self-crossed genome seeds the child with the remaining 25%, fresh IDs, reset
isolation/gene-flow state, and reduced within-lineage variance. Branches join
the generation loop on the following step, and a full eight-slot region cools
the signal without inventing another species ID. The lineage generation field
is the regional clock (a same-step branch shares its parent's generation), so
sampled genomes remain aligned with replay and save/load behavior.

Active-entity births now pass their child genome and species identity into the
regional population boundary. If a child lineage is not yet present, the first
free regional slot is initialized from the child's phenotype and generation,
then the birth biomass is applied to that lineage. A saturated eight-lineage
region merges the event into the matching locomotion archetype so fauna mass is
still conserved. The original lineage-only event API remains for deaths and
harvest feedback; malformed birth genomes are rejected without mutating state.
The same catalog observation boundary is now used for initial passive spawns,
after sampled genome identity and canonical surface coordinates are applied.
This makes the first organisms visible to the biology atlas and gives their
lineage/species records the same save/load and surface-key behavior as later
offspring.

Tests cover nitrogen/phosphorus conservation, leaching accounting, complete
competitive suppression, deterministic repeated steps, directional migration
contracts, speciation branch identity/density conservation, save/load replay,
initial passive-spawn catalog registration, entity birth lineage registration,
and invalid durations.

Persistence regressions force collisions in one four-way cache set, round-trip
1089 distinct regions, load V6 fixtures, reject malformed/truncated V7 checkpoints
without changing live state, and inject cold-store allocation failure. The
eviction regression fails against the previous implementation and passes with
the backing store. Cold records are retained on disk at checkpoints; this is not
a claim of continuous simulation of every visited region.

Regional updates now have an explicit simulation-thread entry point,
`PlanetEcologyAdvanceActiveRegionsBudgetedAt`. It prepares a canonical,
coordinate-sorted neighborhood plus a migration halo, then advances at most the
requested number of complete step batches. Its working set can reference both
hot and cold records, including more than four regions in the same hash set.
Actual per-record update times are the checkpointed catch-up cursors; work is
never discarded by setting an unfinished record's time to the target.

Local population, evolution and genome queries only read committed records.
Unvisited cells get deterministic temporary defaults without allocating storage
or changing cache order. Visual/map/HUD queries therefore cannot activate new
regions or advance time. Birth, death and harvest reject backlogged records
without mutation; the application must prepare the relevant region before
applying those events. Internal legacy adapters remain for focused model tests.

Scheduler tests compare budget 1 and 8, interrupted save/load catch-up, query
ordering, cold-record hash collisions, and rejected events during backlog. The
completed checkpoints are byte-identical across those budget/reload variants.

Both ordinary regional steps and deferred catch-up sample the same procedural
weather baseline at each region's location and historical step time. The light
provider supplies regional daylight for that time, including the persisted home
clock anchor. Remnant exposure is reconstructed from the matching stellar
system at the requested time; custom/loaded snapshots are used only at their
observation time. Reading current exposure cannot move that observation into
the future.

There is no complete event history for forced weather, runtime hydrology or
radiation feedback, or block edits. Regional baseline sampling excludes the
first three; current edits contribute disturbance only at the current regional
step boundary. Live weather and entity hazards continue to use runtime inputs.
This avoids applying today's conditions to an entire absence, but cannot
reconstruct unrecorded interventions. Historical replay tests use populated
regions and compare continuous stepping, budgets 1/8, and checkpointed catch-up
under spatially and temporally varying light, including differing caller light.
