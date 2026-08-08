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
- Water blocks with translucent rendering and simple swimming physics
- New build blocks: plank, brick, glass, water, snow, ice, cactus, bedrock,
  and the four ore types; grab any block you see with middle-click
- Torch block with dynamic point lighting: place torches and nearby block
  faces brighten in a radius, effective at night and underground
- Particle effects on block break/place
- Procedurally synthesized sound effects (no external assets): break, place,
  footsteps (ground/water), splash, pick, and looping rain
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
- Terrain generation AND chunk mesh building run on a background thread
  (meshes are uploaded on the main thread), so new chunks stream in smoothly
  instead of hitching
- Binary save format (V2) with automatic fallback to the old text format
- Image imports use a 256-color block palette for better color matching. Block
  edits are indexed for fast repeated imports and large builds
- Automatic save on quit (plus manual `F5` save / `F9` load); every save
  keeps a backup of the previous file as `voxelcraft_save.bak`
- `F4` switches to a third-person view (camera pulls back, walls occlude);
  `F10` saves a screenshot; the pause menu toggles music, adjusts volume and
  can return to the main menu
- Real planets: besides the distant ringed planet, huge rocky worlds (radius
  8-12) with craters drift in space - land on them and mine them
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
- Space: fly above y=120 to leave the atmosphere - zero gravity (hold Space to
  thrust, Ctrl to brake), a starfield with a ringed planet and galaxy bands,
  and drifting asteroid worlds made of moon rock, meteorite and moon sand.
  Mine asteroids, build in zero-g, and take the blocks back to the surface.
  Space edits are saved with the world
- A real starfield: 500 stars on a fixed celestial sphere rotate as you turn
  (bright ones with cross glints), continuous from ground night skies to space
- Stars are real places: glowing star-matter orbs (self-illuminated blocks)
  drift among the asteroids - land on them, mine them, bring the light home
- Spaceship block: place it (middle-click to pick up), right-click to climb
  in, then fly between stars at up to 30 blocks/s (W/S/A/D thrust, mouse
  steer, Space/Ctrl up/down, E to exit and the ship drops beside you)
- Code is split into modules: terrain, chunks, world, player, interaction,
  render, particles, audio, weather, plus a shared types header

## Build

```sh
make
```

## Run

```sh
make run
```

## Controls

- Start page: choose `Varied` or `Flat`, then click `Start` or press `Enter`
- `WASD` move, `Shift` sprint, mouse look
- `Space` jump, or swim up while in water
- `F` toggle floating mode; in floating mode `Space` up, `Left Ctrl` down
- `Tab` release/capture the mouse
- Left click: break the targeted block
- Right click: place the selected block (ghost preview shows the target cell)
- Middle click: pick the targeted block into the selected hotbar slot
- Mouse wheel: cycle the hotbar selection
- Hotbar: `1` grass, `2` dirt, `3` stone, `4` wood, `5` plank,
  `6` brick, `7` sand, `8` snow, `9` glass, `0` water
- `[` / `]` decrease/increase view distance
- `F6` toggle the day/night cycle
- `F7` cycle the weather manually
- `F8` toggle auto-save
- `F3` toggle the debug HUD
- `F4` third-person view, `F10` screenshot
- Pause menu: music on/off, volume `-`/`+`, return to menu
- Fly above `y=120` to enter space (zero gravity); drop below `y=80` to return
- Spaceship: right-click a placed ship to enter, W/S/A/D + Space/Ctrl to fly,
  E to exit; hunt the glowing star-matter orbs
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
