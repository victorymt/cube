# Stage 06: Tree and plant diversity with biome ecology

## Purpose

Replace anonymous Homeworld tree shapes and hash-only ground cover with a
bounded catalog of real plant taxa. Species identity, growth form, climate and
soil tolerance, disturbance response, rendered material, and deterministic
distribution must agree. Existing extraterrestrial flora remains explicitly
exobiological rather than being mislabeled as Earth species.

This stage consumes the stable climate inputs from Stage 01, the wildfire burn
severity and recovery hook from Stage 04, and the soils and residues from
Stage 05. It does not add free-running plant reproduction or unbounded growth
simulation.

## Stable taxon and block roster

Add stable taxon IDs for six trees and seven smaller plants. Common name,
scientific name, family, growth form, succession role, habitat limits, mature
dimensions, wind response, flammability, and component blocks live in one
read-only catalog.

| Growth form | Taxa | Ecological role |
| --- | --- | --- |
| Broadleaf trees | Pedunculate oak (`Quercus robur`), silver birch (`Betula pendula`), European aspen (`Populus tremula`), white willow (`Salix alba`) | Mature lowland forest, cool disturbed ground, clonal fire pioneer, and wet riparian forest |
| Conifers | Norway spruce (`Picea abies`), Scots pine (`Pinus sylvestris`) | Cold moist forest and cold/dry nutrient-poor terrain |
| Grasses and ferns | Big bluestem (`Andropogon gerardii`), bracken (`Pteridium aquilinum`) | Prairie matrix and forest/disturbance understory |
| Wetland plants | Common reed (`Phragmites australis`), sphagnum moss (`Sphagnum palustre`) | Emergent marsh vegetation and peat-forming ground cover |
| Shrub and pioneer herb | Common heather (`Calluna vulgaris`), fireweed (`Chamaenerion angustifolium`) | Acidic exposed upland and early post-fire succession |
| Desert succulent | Saguaro (`Carnegiea gigantea`) | Hot arid lowland desert |

Append 19 natural blocks after ID 136: a log and foliage block for each tree,
plus one block for each smaller taxon. Existing IDs `0..136`, color IDs
`256..511`, and existing texture IDs remain unchanged. The new range exposes
explicit Stage 06, tree-component, and understory markers. Generic Wood,
Leaves, Flower, Cactus, Fern, and Reed remain valid for old saves and player
builds but Homeworld natural generation uses named taxa.

## Habitat and deterministic selection

- Introduce a pure habitat input and suitability evaluator. Temperature,
  moisture, usable light, elevation, slope, biome, substrate, and burn
  disturbance are scored against catalog traits. Values outside absolute
  tolerances reject a taxon; values near its optimum score higher.
- Build stable Homeworld baseline habitat from the same biome, latitude,
  elevation, water, and soil meanings used by climate and terrain. Tree and
  ground-cover selection is deterministic for world seed and coordinates.
- Use weighted competition among suitable taxa rather than independent
  per-species coin flips. Crown radius controls deterministic tree spacing, so
  adjacent chunks generate the same boundary trees without overlap.
- Stage 05 substrates have explicit ecological meaning: chernozem and
  alluvium favor productive grass/riparian taxa; podzol, peat, and humus favor
  boreal or wet taxa; terra rossa and dry mineral ground exclude wetland taxa;
  ash/charcoal plus a burn record favor pioneers.
- Suitable generated planets continue using their existing alien biomass and
  flora archetypes. Their structure density and morphology consume local
  temperature, moisture, biome support, and carrying capacity instead of
  ignoring the ecology profile. Earth taxon IDs are never assigned there.

## Morphology, artwork, and material behavior

- Oak has a sturdy branching crown; birch a light narrow crown; aspen a tall
  clonal/columnar crown; willow a low spreading and drooping crown; spruce a
  dense conical whorl; pine a taller sparse crown. Height, branching, canopy
  envelope, and bounded within-species variation come from catalog traits and
  coordinate hashes.
- Saguaro grows as a column with bounded raised arms. Grasses, fern, reed,
  heather, and fireweed are crossed foliage; sphagnum is a carpet.
- Every appended block has deterministic diagnostic artwork. Tree bark uses
  distinct side and end-grain tiles; foliage and understory tiles preserve
  transparent padding and remain visually distinct at ordinary play distance.
- Component blocks expose realistic bounded wind, impact, fuel, and erosion
  response. Succulent tissue resists ignition; dry pine, grass, heather, and
  birch litter burn more readily; flexible willow/reed foliage resists wind
  differently from brittle or dense crowns.
- Ordinary harvest, pick, inventory, placement, undo/edit, chunk mesh, and
  persistence paths work for all new blocks. Starter inventory remains
  unchanged.

## Fire succession and recovery

- Convert the Stage 04 burn hook into a pure succession classification:
  unburned, fresh burn, herb pioneer, woody pioneer, and recovering mature.
  Severity and persisted recovery select the stage; the catalog declares
  which taxa participate in each stage.
- Runtime recovery may establish fireweed on eligible loaded ground residue,
  followed by birch/aspen suitability and then the normal mature community.
  It must never create floating plants, overwrite player edits, mutate an
  unloaded block, or read mutable wildfire state from a chunk worker.
- The existing burn record remains the persistence authority. Any additional
  succession metadata must be versioned, transactionally validated, and
  migrated from the Stage 04/05 disk format.
- Taxon fuel traits feed wildfire behavior through component-block material
  profiles. Residues remain nonflammable and severe burns recover more slowly.

## Debug and visual inspection

- Add `flora inspect NAME_OR_ID`, `flora sample X Z`, and
  `flora gallery X Y Z`. Inspect reports taxonomy, growth form, succession,
  habitat bounds, dimensions, response traits, and component blocks. Sample
  reports the stable habitat, substrate, burn stage, suitability, and selected
  tree/ground-cover taxon at a coordinate.
- Gallery validates a loaded bounded region transactionally, places one
  deterministic mature specimen of each tree plus every smaller taxon, and
  records exact counts and bounds. Normal gameplay has no gallery behavior.
- Add typed DSL fields for catalog count, sampled taxa/habitat, burn stage,
  and gallery state. Advance the screenshot debug schema from 11 to 12 and
  include matching flora metadata.

## Tests and acceptance

- [x] Old block/texture IDs remain unchanged; all 19 new block identities and
  the stable 13-taxon catalog are pinned and below color ID 256.
- [x] Catalog names and scientific names are unique; all trait ranges,
  dimensions, component references, biome masks, soil masks, and succession
  roles validate.
- [x] Suitability is finite, deterministic, rejects absolute climate/soil
  mismatches, and scores optima above tolerance edges.
- [x] Fixed seeds produce all Homeworld taxa only in suitable habitats, with
  deterministic density, crown spacing, soil association, and chunk-boundary
  morphology.
- [x] Six tree morphology signatures are distinct and remain inside declared
  height/crown bounds; small-plant render shapes and saguaro arms are correct.
- [x] Alien flora retains its four archetypes and now consumes local ecology
  inputs without acquiring Earth taxon identity.
- [x] Fire severity/recovery deterministically selects pioneer and mature
  stages; loaded-ground establishment is safe and save/load preserves the
  same succession result.
- [x] All appended blocks have unique deterministic atlas artwork, valid
  padding/UV/alpha/material contracts, and ordinary inventory/save behavior.
- [x] `flora inspect`, `flora sample`, `flora gallery`, typed fields, E2E, and
  screenshot schema 12 expose exact taxonomy and ecology state.
- [x] A forced 1920x1080 Medium gallery capture is nonblank and visibly
  distinguishes all six mature tree forms and seven understory taxa.
- [x] Focused tests, `make test`, `make test-sanitize`, `make test-long-run`,
  `make test-e2e`, `git diff --check`, and the standard Medium performance
  route pass.
- [x] The codebase-memory index is refreshed and Stage 06 is committed alone,
  excluding `AGENTS.md` and generated screenshots.

## Verification evidence

- The stable catalog contains 13 taxa and maps to block IDs `137..155`; the
  total catalog is 412 blocks with 92 natural blocks. Unit tests pin the old
  Stage 05 endpoint at 136, the color boundary at 256, all catalog identities,
  unique common/scientific names, component mappings, material contracts, and
  inventory/save behavior.
- Habitat tests exercise each taxon at its optimum and tolerance edges, reject
  invalid temperature and substrate inputs, and pin deterministic selection.
  Fixed-seed terrain tests cover spacing over a 193x193 region, all six named
  tree rasters and ownership bounds, distinct shape signatures, cross-chunk
  crowns, full-column resolution, and named ground-cover wind metadata.
- Homeworld structure ownership uses the same shared deterministic recipe as
  placement. Named logs and translucent leaves are both emitted through the
  flora mesh, excluded from the solid mesh, retain taxon and wind identity,
  and remain identical across intersecting chunks. Alien structures retain
  `taxonId=-1` and their four archetypes, while morphology scales with the
  generated ecology profile.
- Wildfire tests prove all six named log/foliage pairs consume fuel and create
  the intended residues. Recovery establishes fireweed only on loaded,
  supported, unobstructed ash; it preserves player blocks and survives the
  existing versioned `WXIMPACT2` save/load path and legacy migration.
- The DSL commands report exact taxonomy and habitat state. E2E produced
  `flora gallery ... placed=573 trees=6 ground=7 taxa=13`, rejected invalid
  galleries transactionally, and wrote screenshot schema 12 with the same
  counts. The retained artifacts are
  `/tmp/voxelcraft-stage06-e2e/stage06-flora-gallery.{png,txt}`.
- The retained Medium PNG is 1920x1080 RGBA, nonblank, with 7,750 sampled
  colors and luminance `0..255`. Visual inspection shows distinct broad,
  narrow/columnar, drooping, dense-conical, and sparse-conifer silhouettes,
  different bark/foliage palettes, and the seven ground specimens; its report
  records all 13 taxa and the exact 6/7 gallery split.
- `make test` passed all 74 tests plus public-header, module-archive,
  clean-build, and architecture gates. `make test-sanitize` passed the same 74
  tests with ASan/UBSan and `-Werror`; `make test-long-run` passed;
  `make test-e2e` passed with a zero-issue ravine stream audit; and
  `git diff --check` passed. The production hotspots remain within limits:
  `src/world/chunks_mesh.c` is 1,500 lines and `src/app/game_debug.c` is 2,044.
- The standard Medium performance route passed schema 3 with 720 route frames,
  120 warmup frames, and 600 samples. CPU frame p95 was 10.489 ms, GPU frame
  p95 was 0.551 ms, generation and mesh jobs completed `847/847` and
  `2779/2779`, final pending mesh snapshot bytes were zero, worker/queue and
  resource stability targets passed, and `sync_rebuilds=0`. The report is
  `/tmp/voxelcraft-stage06-perf.keyvalue`.
- A full codebase-memory refresh for `home-cv-project-temp-cube` was completed
  after implementation and verification. Stage 06 is committed independently
  without `AGENTS.md`, generated screenshots, save files, or performance
  artifacts.

## Out of scope

Individual plant age persistence, seeds/genetics, seasonal leaf color or leaf
drop, fruit/crop systems, root excavation, falling-tree physics, full forest
stand competition, unbounded off-screen dispersal, and global coordinate
topology. Stage 07 handles planet wrapping and poles; Stage 08 audits the
combined systems and migration boundaries.
