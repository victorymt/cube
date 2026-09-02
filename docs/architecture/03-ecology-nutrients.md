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
query. The adapter is still a bounded regional approximation: migration moves
the normalized flora signal, but does not yet transfer individual nutrient
pools between neighboring records.

Tests cover nitrogen/phosphorus conservation, leaching accounting, complete
competitive suppression, deterministic repeated steps, and invalid durations.
