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

The module is intentionally stateless and independent of rendering. It is not
yet wired into `ecology_population.c`; the adapter must map each regional
record's flora density, climate suitability, and rainfall to a cell state and
persist the additional pools. Age and seed-bank state are included now so that
future plant succession can be added without changing the conservation API.

Tests cover nitrogen/phosphorus conservation, leaching accounting, complete
competitive suppression, deterministic repeated steps, and invalid durations.
