# Voxelcraft

A small Minecraft-style voxel sandbox built with raylib. The map is generated
from infinite horizontal chunks around the player, with block edits preserved
while the game is running. Chunks are rendered as cached visible-face meshes and
rebuilt only when nearby terrain changes. Block textures are generated at
startup as a small pixel-art atlas.

## Features

- Procedural world with five biomes: plains, forest, desert, snow, and
  mountains, each with its own surface blocks, tree density, and height profile
- Caves and ore veins (coal, iron, gold, diamond) underground, plus a bedrock
  floor; desert cacti and frozen ponds in low snow valleys
- Day/night cycle (4-minute loop) with animated sky, sun, moon, stars, clouds,
  and world lighting; pause it anytime with `F6`
- Unified environment presentation across Homeworld, planet surfaces, space,
  and the Nether: natural exposure and color response, weather-aware distance
  fog, wet surfaces, wind-driven reflective water, cloud cover, lightning,
  and scene-specific atmosphere; unsupported shader paths fall back safely
- Low/medium/high graphics quality presets control shadow resources, cloud
  range, and precipitation density. Medium is the default 1080p/60 FPS target
- Water blocks with translucent rendering and simple swimming physics
- New build blocks: plank, brick, glass, water, snow, ice, cactus, bedrock,
  and the four ore types; grab any block you see with middle-click
- Torch block with dynamic point lighting: place torches and nearby block
  faces brighten in a radius, effective at night and underground
- Particle effects on block break/place
- Layered environmental audio crossfades between rain, wind, forest, water,
  caves, the Nether, and ship interiors. Three compact CC0 recordings are
  distributed with source/license records in `assets/LICENSES.md`; all other
  effects and missing-asset fallbacks are synthesized at startup
- Weather system: clear / rain / snow cycles; snow favors cold biomes and
  high altitudes; sky and clouds darken when stormy
- Auto-save every 60 seconds (toggle with `F8`)
- Undo/redo your edits with `Ctrl+Z` / `Ctrl+Y`
- Photo album (`P`): import images from any folder with live preview, browse
  them in a paged grid (arrow keys / wheel / page buttons), remove with
  `Delete`, or press `Space` to paint the selected image into the world as
  colored blocks. The album is also a placeable block: pick it up with
  middle-click, place it like any block, and right-click the placed album
  to open it. Album contents are saved with the world
- New blocks: stone slab (walkable half-height) and wooden doors (right-click
  to open/close)
- World structures: pine trees in snow biomes, a canyon system carved into the
  terrain with water at the bottom, and small wooden village houses
- Terrain generation and chunk mesh building run in the background, while space
  chunks use a 16-job worker queue that prioritizes the chunks nearest the
  player. Meshes are uploaded on the main thread so new areas stream in smoothly
  instead of hitching
- Binary save format (V12) persists the world seed, inventory, ship fuel and locator,
  Homeworld/space mode, planet-world context, per-planet edit dimensions,
  entities, and ecology state; it remains compatible with V2-V11 and old text
  saves. Legacy saves are migrated in memory and rewritten as V12 only after
  the next successful save
- Image imports use a 256-color block palette for better color matching. Block
  edits are indexed for fast repeated imports and large builds
- Automatic save on quit (plus manual `F5` save / `F9` load); every successful
  save is written through a temporary file and atomic replacement, and keeps a
  synced backup of the previous file as `voxelcraft_save.bak`. Failed saves
  leave the existing main file unchanged
- The start page accepts a deterministic world seed (or generates one at
  random); starting again after returning to the menu creates a clean world
- `F4` switches to a third-person view (camera pulls back, walls occlude);
  `F10` saves a timestamped PNG and same-name debug report under `screenshots/`;
  the pause menu
  selects graphics quality, controls
  master/environment/music volume separately, and can return to the main menu.
  Preferences are atomically saved in `voxelcraft_settings.cfg`, separately
  from the world save
- Every generated solid planet is a world: space uses a spherical proxy, while
  landing streams a deterministic, effectively unbounded chunk surface with
  planet-specific terrain, materials, sky colors, caves, liquids, and landmarks.
  Gas giants have deep atmospheres and gravity but no fake solid landing surface
- Minecraft-style content: stairs, fences, fence gates, glass panes, lava
  (swimmable, glowing), flowers and mushrooms on the surface, bookshelves,
  hay bales, pumpkins; 3D noise caves with water pools, abandoned mineshafts
  with timber supports, stone-brick dungeons, desert temples
- The Nether: build an obsidian frame with a nether portal inside and
  right-click it to travel to a red-lit underworld of netherrack, soul sand,
  lava lakes and glowing glowstone; portal back to return. Nether edits save
- Animals (cows, sheep, pigs, chickens) roam the daytime world; zombies and
  skeletons hunt you at night (zombies burn in sunlight). Left-click to
  defeat them
- Meteors streak across space skies
- Underwater tint when the camera is submerged (plus splash sound and bubbles
  while swimming), a soft procedural ambient music loop, and an `F3` debug HUD
  (position, FPS, chunk/mesh queues, particles, weather, time, ship speed)
- Space: fly above y=120 to leave the atmosphere - zero gravity (Space/Ctrl
  move up/down), a starfield, galaxy bands, named planetary systems,
  and drifting asteroids made of moon rock, meteorite and moon sand.
  Mine asteroids, build in zero-g, and take the blocks back to the surface.
  Space edits are saved with the world. The X/Z space is procedurally streamed
  without a gameplay boundary; chunks still generating do not block the ship
- A real starfield: every visible star is a deterministic, named system from
  the star map, with its own reachable coordinates and two to six planets;
  the same stars remain continuous from ground night skies into space
- Planets follow inclined Kepler-style orbits: each system has a distinct
  three-dimensional orbital plane with small planet-to-planet deviations;
  inner worlds move faster than outer worlds, and every planet rotates on its
  axis while orbiting its star
- A deterministic physical profile links each planet's orbit and host-star
  luminosity to temperature, then derives mass, surface gravity, atmosphere,
  ocean coverage, terrain roughness, rotation, rings, space appearance and
  surface generation from the same identity
- Stars are real places: glowing star-matter orbs (self-illuminated blocks)
  drift among the asteroids - land on them, mine them, bring the light home
- Spaceship block: place it (middle-click to select), right-click to climb
  in, then fly between stars at up to 30 blocks/s (W/S/A/D thrust, mouse
  steer, Space/Ctrl up/down). Approach a named planet and press E to land in
  its surface world; take off above the atmosphere to return to the same orbit
- Code is split into modules: terrain, chunks, world, world_environment, player, interaction,
  render, particles, audio, weather, plus a shared types header

## Build

```sh
make
```

The Linux release gate is `make release-check`. It builds a versioned archive
under `dist/` and writes a SHA-256 checksum beside it. Release archives do not
include user save files.

Run a deterministic performance route with `./voxelcraft --perf`. Use
`--perf-report PATH` to choose the key/value report and `--perf-baseline PATH`
to enforce the 5% CPU/GPU/upload and estimated live-mesh regression gates.
Performance reports use schema v3; older baselines are rejected and must be
recaptured. Resource stability is measured after chunk generation, meshing, and
uploads settle. Mesh byte fields estimate public raylib `Mesh` buffers and are
not driver-reported VRAM.

For scripted visual debugging, start the game with `--debug-stdin`. It accepts
line-delimited commands: `start`, `screenshot`, `status`, `teleport X Y Z YAW
PITCH`, `input FORWARD STRAFE VERTICAL SPRINT FRAMES`, or `quit`. Movement
components are clamped to `[-1, 1]`, sprint is `0` or `1`, and an input window
lasts 1-600 fixed 60 FPS frames. Debug sessions ignore desktop keyboard state
outside those input windows, so runs are reproducible. Replies begin with
`DEBUG_CONTROL`; status includes player water flags, actual water surface and
depth, and a successful screenshot reply contains both the PNG and TXT report
paths. The interface is disabled during a normal launch.

## Run

```sh
make run
```

## Controls

- Start page: choose `Varied` or `Flat`, enter or randomize the world seed, then
  click `Start` or press `Enter`
- `WASD` move, `Shift` sprint, mouse look
- `Space` jump, or swim up while in water
- `F` toggle floating mode; in floating mode `Space` up, `Left Ctrl` down
- `Tab` release/capture the mouse; `M` opens the star map from Homeworld or space
- `L` toggles the main ship locator; local ships get a direction and distance,
  while ships in another world show their last parked location
- Left click: break the targeted block
- Right click: place the selected block (ghost preview shows the target cell)
- Middle click: select the targeted block type into the selected hotbar slot
- Mouse wheel: cycle the hotbar selection
- Hotbar: `1` grass, `2` dirt, `3` stone, `4` wood, `5` plank,
  `6` sand, `7` snow, `8` glass, `9` water, `0` spaceship. Breaking blocks
  collects them and placing blocks consumes them
- `[` / `]` decrease/increase view distance
- `F6` toggle the day/night cycle
- `F7` cycle the weather manually
- `F8` toggle auto-save
- `F3` toggle the debug HUD
- `O` toggle nearby planetary orbit trajectories
- `F4` third-person view, `F10` debug screenshot (PNG plus a same-name
  `key=value` TXT report under `screenshots/`)
- Pause menu: low/medium/high graphics quality, master/environment/music
  volume, music on/off, save, and return to menu (`-`/`+` still adjust master)
- Fly above `y=120` to enter space; approach Homeworld and press `E` to return
- Spaceship: right-click a placed ship to enter, W/S/A/D + Space/Ctrl to fly,
  E to exit or land when a planet prompt is visible; rise above the planet's
  atmosphere to return to orbit. Press `R` while flying to restore the unlimited fuel tank
- Nether portal: obsidian frame + portal block, right-click to travel
- `Ctrl+Z` / `Ctrl+Y` undo / redo block edits
- `P` open the photo album (Enter add image, Space paint it into the world, Delete remove)
- Album block: middle-click a placed album to pick it up, place it like any
  block, right-click it to open the album
- Slab: place like any block, walk over it (half-height); middle-click to pick
- Door: place like any block, right-click to open/close
- `F5` save map to `voxelcraft_save.txt`; `F9` load map from it
- Flat mode: press `I`, enter or paste a local image path, use `Tab` to toggle
  grayscale relief or flat color mode, adjust precision from 16 upward with
  `[`/`]` or `-`/`+`, then press `Enter`
- `F1` toggle help
- `Esc` open the pause menu (Resume / Save World / Save & Quit); the world and
  day/night cycle freeze while paused
