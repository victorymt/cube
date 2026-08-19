# Voxelcraft world expansion roadmap

This directory is the implementation and acceptance record for the long-term
world-expansion work. A stage is planned here before implementation, updated as
work progresses, verified, and then committed as one reviewable change.

## Final goals

1. Expand block content across biological, geological, soil, plant, and useful
   material families. New blocks must participate in generation, rendering,
   inventory/building, persistence, and relevant simulation systems.
2. Make Homeworld and other suitable planets globally traversable. Longitude
   must wrap without a terrain, climate, lighting, physics, or persistence seam;
   latitude must have explicit pole behavior rather than an artificial edge.
3. Build a realistic weather and natural-phenomena system. It must include
   physically related local climate, multiple cloud layers and types, severe
   storms including tornadoes, wildfire, environmental effects, debug control,
   persistence where appropriate, and bounded runtime cost.
4. Expand trees and plants using real-world ecological families. Distribution,
   growth form, climate tolerance, succession, and weather response should be
   data driven rather than cosmetic-only variants.

## Stage order

| Stage | Scope | Status |
| --- | --- | --- |
| 01 | Local climate and weather foundation | Complete |
| 02 | WMO cloud taxonomy and layered cloud fields | Complete |
| 03 | Tornado lifecycle, forces, debris, and presentation | Complete |
| 04 | Persistent wildfire, smoke, suppression, and recovery hooks | Complete |
| 05 | Geological and biological block expansion | Complete |
| 06 | Tree and plant diversity with biome ecology | Complete |
| 07 | Global planetary topology and seamless traversal | Planned |
| 08 | Cross-system integration, migration, and performance audit | Planned |

The order deliberately establishes climate inputs before ecology and global
topology. Stage 07 may require migration of spatial keys and saves, so it is
kept separate from content expansion and will include an explicit compatibility
plan before code changes begin.

## Shared acceptance rules

- Add deterministic unit coverage for simulation rules and save migrations.
- Add DSL or `--debug-*` controls for runtime-only reproduction and inspection.
- Reproduce visual behavior in the running Hyprland build where applicable.
- Keep normal gameplay free from test-only behavior.
- Validate the stage with the narrow tests first, then the repository gates
  proportionate to the affected systems.
- Update the stage document with exact evidence and known limits before commit.
- Commit every completed stage separately; do not include `AGENTS.md`.
