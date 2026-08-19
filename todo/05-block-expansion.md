# Stage 05: Geological, soil, biogenic, and fire-residue blocks

## Purpose

Expand the material vocabulary of Homeworld and generated planets with
recognizable real-world rock cycles, ores, soils, biological deposits, and
post-fire residues. Every added block must be more than a catalog swatch: it
must have stable identity, deterministic artwork, physical material response,
natural or simulation-driven provenance, ordinary harvest/place behavior,
inventory and save compatibility, and automated distribution coverage.

This stage deliberately avoids adding tree species or decorative plant taxa.
Those require growth form and climate-distribution data and remain Stage 06.

## Stable block roster

Append the following blocks after the existing natural range. Existing block
IDs `0..110`, color IDs `256..511`, and existing texture IDs remain unchanged.

| Family | Blocks | Real-world role |
| --- | --- | --- |
| Volcanic and intrusive | Andesite, Diorite, Rhyolite, Tuff | Intermediate/felsic lava, intrusive bodies, and consolidated volcanic ash |
| Metamorphic | Schist, Slate, Serpentinite | Regional metamorphism, low-grade shale metamorphism, and altered ultramafic rock |
| Carbonate and evaporite | Dolomite, Gypsum, Travertine | Dolostone platforms, evaporite beds, and spring carbonate deposits |
| Economic geology | Bauxite, Hematite Ore, Magnetite Ore, Phosphate Rock | Aluminium feedstock, distinct iron ores, and phosphate sediment |
| Pedology | Chernozem, Terra Rossa, Alluvium | Fertile grassland soil, carbonate-derived red soil, and floodplain sediment |
| Biogenic substrates | Leaf Litter, Humus, Compost, Shell Bed, Coral Limestone, Guano | Surface detritus, organic horizons, decomposed matter, shell/coral carbonate, and cave nutrient deposits |
| Fire residues | Charred Wood, Charcoal, Fire Ash | Woody remnants, low-oxygen carbonized fuel, and fully oxidized fine residue |

The final appended range is contiguous but exposes explicit geology,
biogenic, and fire-residue range markers. Tests pin every old sentinel and all
new endpoints so later stages cannot silently renumber saves.

## Catalog, rendering, and material behavior

- Add a catalog entry and a unique procedural texture for every new block.
  Rock artwork reflects diagnostic structures: volcanic speckle and vesicles,
  slate cleavage, schist foliation, serpentinite veining, evaporite crystals,
  ore bands, fossil/shell fragments, and layered organic material.
- Leaf litter is a non-colliding carpet. Other soils, rocks, deposits, and fire
  residues are cubes so they can participate in ordinary terrain and building.
- Material response follows composition. Competent igneous/metamorphic rock
  resists wind and impact; unconsolidated soil, guano, ash, and litter erode
  readily; organic substrates have bounded fuel; all mineral blocks and fully
  burned residues are nonflammable.
- Atlas dimensions continue to derive from `TEX_COUNT`. Per-tile and whole
  atlas digests, padding, UV, alpha, and material-profile tests remain strict.

## Generation and provenance

- Extend Homeworld strata with coherent deterministic regions rather than
  per-block salt-and-pepper selection. Depth, biome, regional field, and
  stratal field select volcanic, intrusive, sedimentary, and metamorphic rocks.
- Extend ores with separate bounded bands for hematite, magnetite, bauxite,
  and phosphate. Existing rare-resource precedence remains stable.
- Add soil horizons: chernozem in plains, humus and litter in forests,
  alluvium in wet lowlands and coastal shelves, terra rossa over warm carbonate
  terrain, and uncommon compost-rich pockets in swamp/forest organic zones.
- Shell beds and coral limestone occur in suitable shallow marine sediment;
  guano occurs only on dry cave floors. These rules never overwrite liquids,
  bedrock, protected structures, or explicit player edits.
- Generated planet styles use the same material meanings: volcanic worlds gain
  andesite/rhyolite/tuff; temperate coasts and forests gain carbonate and
  biogenic substrates; deserts gain gypsum/dolomite/terra rossa; crater/ice
  worlds gain appropriate iron-bearing and altered rocks.
- Wildfire replaces consumed fuel with a deterministic residue based on fuel
  family, burn severity, and moisture. Woody fuels can leave charred wood or
  charcoal; light/dry fuel becomes fire ash. Residues use the existing
  environment-mutation and burned-site persistence paths and cannot reignite.

## Inventory, persistence, and interaction

- All appended natural blocks use the existing harvest, middle-pick, inventory,
  placement, undo/edit, chunk mesh, and save paths. The starter kit grants none
  of them.
- Inventory remains a fixed 512-entry payload, so adding IDs below 256 does not
  change its disk size. Existing block edits remain valid because old IDs are
  never renumbered. New edit IDs round-trip and invalid IDs still reject the
  entire transactional load.
- Add direct tests for harvestable inventory slots, full save/load round trips,
  old sentinel IDs, and the new natural endpoint.

## Debug and visual inspection

- Add `block inspect NAME_OR_ID` and `block gallery X Y Z`. Inspect reports ID,
  canonical name, texture IDs, shape, collision, translucency, and physical
  response. Gallery transactionally verifies a loaded bounded region and then
  lays out all Stage 05 blocks in deterministic labeled-order rows.
- Add typed DSL fields for catalog/natural/Stage 05 counts and gallery state.
  Advance the screenshot debug schema from 10 to 11 and report the same block
  catalog and gallery metadata.
- E2E builds a gallery on a loaded Homeworld surface, inspects representative
  geology/biogenic/fire blocks, harvests or places through normal contracts
  where practical, captures a nonblank 1920x1080 Medium image, and verifies the
  report. Normal completion and stdin EOF still leave the process running.

## Tests and acceptance

- [x] Existing block and texture IDs are unchanged; all 26 appended IDs and
  their range markers are pinned below color ID 256.
- [x] Every new block has a nonfallback name, unique deterministic artwork,
  valid UV/padding, material response, shape, collision, and alpha contract.
- [x] Homeworld and planet rules deterministically produce every new geology,
  soil, and biogenic block in an ecologically/geologically suitable context.
- [x] Ores and deposits are bounded, deterministic, spatially coherent where
  appropriate, and absent from flat terrain and invalid/protected locations.
- [x] Wildfire produces charred wood, charcoal, or fire ash without reignition,
  preserves burned-site accounting, and round-trips residues as block edits.
- [x] All new blocks can be harvested, picked, inventoried, placed, and saved;
  starter inventory and old save payload sizes remain unchanged.
- [x] `block inspect`, `block gallery`, typed fields, E2E, and screenshot schema
  11 expose the exact catalog and gallery state.
- [x] A forced 1920x1080 Medium gallery capture is nonblank and visually
  distinguishes the geological, biogenic, and fire-residue rows.
- [x] `make test`, `make test-sanitize`, `make test-long-run`, `make test-e2e`,
  `git diff --check`, and the standard Medium performance route pass.
- [x] The codebase index is refreshed and Stage 05 is committed independently
  without `AGENTS.md` or generated screenshots.

## Verification evidence

- Identity, inventory, and artwork: `test_inventory` pins the old range,
  every appended ID `111..136`, the `256..511` color range, all 512 inventory
  slots, the unchanged 1,024-byte payload, transactional corrupt-load
  behavior, and an empty Stage 05 starter inventory. `test_block_atlas` pins
  every Stage 05 tile, confirms unique artwork and material/shape/alpha
  contracts, and records whole-atlas digest `0x785aef9a355d43dc`.
- Generation and provenance: `test_terrain_scale` covers direct Homeworld and
  planet geology rules, bounded ores, actual-coordinate searches for all nine
  new soil/biogenic deposits, dry-cave guano, leaf-litter decoration, and flat
  terrain exclusion. Updated deterministic terrain digests are
  `8452164288714417906`, `1560829849132147165`,
  `15945370240875556352`, and `5099965485900159630`.
- Fire and persistence: `test_weather_impact` verifies deterministic ash,
  charred-wood, and charcoal selection, environment-source mutation,
  non-reignition, burn-site accounting, and save/load. `test_chunk_streaming`
  installs Stage 05 edit IDs and proves an invalid ID or Y coordinate leaves
  the existing edit batch unchanged. The E2E save/load transaction restores
  all 36 edits in its isolated runtime directory, including the 26-block
  gallery.
- DSL and visual acceptance: `make test-e2e` resolves `Andesite`,
  `coral_limestone`, and ID `136`; rejects an unloaded gallery without
  terminating the stdin-controlled process; then places all 26 blocks in one
  undo group and verifies typed state plus screenshot schema 11. The retained
  evidence is `/tmp/voxelcraft-stage05-artifacts.DvtoHD/`:
  `stage05-block-gallery.png` and `.txt`, a 1920x1080 Medium capture with help
  hidden and three visibly separate 14/9/3 family rows. PNG validation sampled
  6,487 colors with luminance spanning 0-255; the PNG checksum is
  `2002939826 4195022`.
- Repository gates: normal and ASan/UBSan runs each report `73 passed, 0
  failed`; public-header, module, clean-build, and architecture checks pass.
  `make test-long-run`, `make test-e2e`, and `git diff --check` pass.
- Standard Medium performance report:
  `/tmp/voxelcraft-stage05-perf.keyvalue`, schema 3, status `pass`, 720-frame
  route with 120 warmup and 600 samples. CPU frame mean/p95 is 3.896/5.958 ms;
  GPU is 0.862/1.122 ms; upload p95 is 0.055 ms; `sync_rebuilds=0`; generation
  and mesh queues drain; queue-capacity, worker, resource-stability, and sync
  rebuild targets all pass.
- The codebase-memory repository index was refreshed in full after
  implementation. `AGENTS.md` remains untracked; the Stage 05 commit excludes
  it and all generated screenshots.

## Out of scope

Crafting/refining chains, tools and mining hardness timers, ore processing,
structural collapse, named tree/plant species, ecological succession, and
global coordinate topology are later work. Stage 06 consumes the soil,
biogenic, and burn-residue families for plant distribution and succession;
Stage 07 changes planetary topology without renumbering this catalog.
