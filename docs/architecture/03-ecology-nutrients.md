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

The additional pools are persisted in population state version 6. Versions 1
through 5 remain loadable; legacy records lazily initialize their nutrient
state from the saved flora density on the next population step or evolution
query. Migration now exposes both net and gross directional flora/fauna fluxes.
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

Tests cover nitrogen/phosphorus conservation, leaching accounting, complete
competitive suppression, deterministic repeated steps, directional migration
contracts, speciation branch identity/density conservation, save/load replay,
entity birth lineage registration, and invalid durations.
