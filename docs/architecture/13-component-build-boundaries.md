# Component Build Boundaries

## Ownership

Production code is owned by seven top-level components under `src/`: `core`,
`world`, `space`, `ecology`, `gameplay`, `presentation`, and `app`. Public
headers form the contract for a component; implementation-only details remain
in `*_internal.h` or `*_dependencies.h` files. The architecture test rejects
unqualified local includes and direct imports that bypass these contracts.

## Build seam

`mk/modules.mk` discovers sources within each ownership directory, while
`mk/components.mk` is the single ownership manifest. The Makefile compiles each
source to its own dependency-tracked object. Those objects are collected into
one static archive per component under
`build/<variant>/modules/`. The executable links the seven archives in a
`--start-group`/`--end-group`, which handles the existing narrow cross-component
calls while keeping the artifacts independently rebuildable.

Use `make component-world` (or the corresponding target for another component)
to rebuild one archive. The target and archive recipe are generated from the
manifest, so a new ownership domain has one declaration point. `make components`
builds the complete set, and `make -j components plugins` can build built-in
components and optional shared plugins concurrently.

## Dependency direction

`core` contains neutral infrastructure and cannot include higher-level domain
headers. `world`, `space`, `ecology`, and `gameplay` expose state through value
types, snapshots, and callbacks rather than mutable globals. `presentation`
consumes those contracts for rendering and audio. `app` is the composition root:
it owns the game loop, wires callbacks, coordinates persistence, and links the
components without moving domain policy into another component.

Resource accounting follows the same direction. `core/render_resources.h` owns
the `RenderResourceSnapshot` contract and its bounded mesh-size aggregation;
`world` and `space` can publish resource metrics without importing a
`presentation` header. The presentation component consumes the resulting values
for diagnostics, but does not own the shared accounting primitive.

Lighting follows the same value-boundary rule. `world/world_lighting.h` owns only
the neutral `WorldLightingState` value type. Presentation-only fallback selection,
underwater fog palette, and composition live in `presentation/world_lighting.c`;
the world component therefore does not need to include environment presentation
or renderer headers.

Weather sampling also crosses the boundary as data. `world/weather.h` exposes
interpolated `WeatherFieldSample` values without importing visual types. The
`presentation/weather_presentation.c` adapter combines those physical samples
with atmosphere state and delegates visual derivation to `weather_visual.c`.
This removes the previous `world/weather.h` dependency on presentation.

Runtime ecology enters world generation through `world/ecology_hooks.h`.
The contract carries only flora capacity, visual values, and a chunk decoration
callback; `app/game_biology.c` adapts the ecology component to those hooks.
Terrain generation and chunk visuals therefore no longer include or call the
high-level ecology runtime API directly.

Active surface state follows a value-snapshot boundary.
`world/world_surface_snapshot.h` owns the surface identity, canonical map
origin, generation seed, and copied planet profile consumed by world systems.
The app composition root derives that snapshot from the space component and
installs it only while chunk generation is drained, including startup, new
world, load/rollback, landing, launch, and debug transitions. World code no
longer imports `space/space_state.h` or calls the high-level home/planet state
getters while generating terrain, weather, and chunks.

Simulation time and planet lighting remain explicit inputs rather than hidden
state lookups. The app passes one periodic simulation time through
`WeatherUpdate`, chunk ecology visuals, and entity ecology sampling; legacy
weather sampling wrappers use the most recent update time. Planet illumination
enters weather through the neutral `WeatherSurfaceLightProvider` callback, with
finite clamped values at the boundary.

## Verification

- `make test-modules` builds all seven archives.
- `make component-world` demonstrates an isolated component build.
- `make test-architecture` checks archive composition, independent targets,
  include boundaries, private-state rules, and rejects world regressions to
  high-level space state access.
- `make test` and `make test-sanitize` cover the composed executable and the
  component-level unit-test matrix.
