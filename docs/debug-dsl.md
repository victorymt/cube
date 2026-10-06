# Debug DSL

The debug DSL drives a running Voxelcraft window through the existing debug
control channel. It is intended for deterministic visual checks, streaming
diagnostics, and repeatable bug reports. The debug path is Wayland-only and is
designed for the active Hyprland session.

## Prerequisites

Build the normal binary first:

```sh
make
```

Run it from an active Hyprland/Wayland session. `WAYLAND_DISPLAY` must name a
socket below `XDG_RUNTIME_DIR`, and the session must provide a usable GPU. The
debug runner opens a real window; it does not use X11, Xvfb, or a software
rendering fallback. The project's graphical smoke test therefore requires the
same session:

```sh
make test-e2e
# Short pause/map/surface/view/look/mouse/ship smoke test:
make test-debug-graphics-e2e
# Independent north/south pole route with 600 input frames:
make test-polar-e2e
```

The command-line and batch-script error contract is covered without a window
by `make test` (the `test-debug-cli` target). Keep `make test-e2e` for tests
that exercise actual frame updates and rendering; it runs both the long
streaming regression and the short graphics-control smoke test.

`make test-polar-e2e` runs `tests/scenes/polar_replay.dsl` at 1920x1080 with a
90-second wall timeout (`POLAR_E2E_TIMEOUT` overrides it). The route checks pole
rebases, canonical weather/ecology coordinates and duplicate chunks, then reports
progress after each of six 100-frame input segments. This preserves the full
600-frame soak while making slow progress visible.

Failed E2E runs retain their temporary runtime directory, trace, replies and
screenshots and print their paths. To retain a successful run, use
`E2E_KEEP_ARTIFACTS=1 make test-e2e` or
`POLAR_E2E_KEEP_ARTIFACTS=1 make test-polar-e2e`. The tests check the existing
Wayland socket and use DSL commands; they do not require `hyprctl`.

Synchronous shell clients should consume a block's terminal `DEBUG_SCRIPT complete`,
`error` or `exit` reply before sending the next block. A `DEBUG_CONTROL` callback reply
alone does not mean the DSL block has completed. The graphical harnesses share
`tests/debug_stdin_protocol.sh`; `make test-debug-stdin-protocol` verifies reply
ordering, expected errors, multiline blocks, deferred captures and timeouts
without opening a window.

## Starting a debug session

### Landform diagnostics

Version 7 and later non-flat Homeworld terrain supports `landform inspect` and
`landform find TYPE RADIUS` (radius 1–16384 surface blocks). Types are `upland`,
`river`, `dry_channel`, `lake`, `wetland`, `floodplain`, `fan`, and `terrace`.
Terraces occur only in version 8 and later worlds. Their search also samples
both valley shoulders at five channel widths. They represent static abandoned
valley floors, not live river migration. `tests/scenes/landform_terrace.dsl`
checks discovery, dry sediment, save/load and captures the default seed.
Inspection prints elevation, water level, watershed/reach IDs, catchment area,
slope, wetness, sediment thickness, material fractions and generation version.
`find` samples a fixed set of atlas vertices, reach centers and banks within
the requested spherical distance; it returns the closest matching candidate,
not an exhaustive search of every voxel. No match is a successful query with
`landform.found == false`. Neither command changes the world or moves the player.

DSL fields: `landform.available`, `landform.found`, `landform.position` (last
found safe viewing position in local coordinates), `landform.kind`,
`landform.has_water`, `landform.elevation`, `landform.water_level`,
`landform.area`, `landform.slope`, `landform.wetness`, `landform.sediment`,
and `landform.version`. Check availability and found before using the position;
repeat the query after changing worlds. These describe generated terrain,
not subsequent player edits or simulated fluid levels.

```text
landform find river 4096
assert landform.found
let river = landform.position
teleport ${river.x} ${river.y} ${river.z} 0 -0.6
landform inspect
```

There are two entry points:

```sh
build/normal/voxelcraft --debug-stdin
build/normal/voxelcraft --debug-script PATH
build/normal/voxelcraft --debug-script=PATH
build/normal/voxelcraft --debug-resolution 1920x1080 --debug-script PATH
build/normal/voxelcraft --chunk-workers auto --debug-script PATH
build/normal/voxelcraft --plugin-dir build/normal/plugins --debug-script PATH
```

`--debug-script` accepts exactly one readable file. A missing path, an empty
path, a path that is too long, or a repeated option is a startup error. The
file is loaded before the first frame. `--debug-stdin` reads complete DSL
blocks from standard input. The options can be combined; the file is loaded
first and stdin is then available.

`--debug-resolution WIDTHxHEIGHT` sets the initial window size before the
first frame; the `--debug-resolution=WIDTHxHEIGHT` form is equivalent. Width
must be 320-7,680 pixels and height 240-4,320 pixels. A missing, malformed,
out-of-range, or repeated resolution option is a startup error with exit code
2. The option only controls window creation and does not enable the DSL by
itself.

`--chunk-workers auto|1-8` controls the surface terrain and mesh worker pool.
`auto` is the default and uses half of the online logical CPUs, clamped to
1-8 workers so rendering, audio, and the compositor retain CPU time. An
explicit count is useful for low-power systems and repeatable comparisons.
Without this option, the saved `Chunk workers` pause-menu setting is used.
Supplying the option, including `--chunk-workers auto`, locks that menu row
for the session so deterministic debug and performance runs cannot be changed
accidentally. Menu changes are coalesced while paused and applied at the next
safe frame boundary after the pause menu closes.

### Multicore tuning recipe

Use the same DSL script for each worker count. This keeps the world seed,
starting position, stream wait, and frame schedule identical while only the
worker-pool request changes:

```sh
cat >/tmp/chunk-workers.dsl <<'DSL'
assert game.screen == "start"
start
wait until game.screen == "playing" timeout 7200
teleport 0 96 0 3.141593 -0.25
stream wait 600
assert stream.surface_ready
assert stream.audit_complete
assert stream.workers_started > 0
status
screenshot
exit 0
DSL

for workers in auto 2 4; do
  build/normal/voxelcraft \
    --chunk-workers "$workers" \
    --debug-trace "/tmp/chunk-workers-$workers.jsonl" \
    --debug-script /tmp/chunk-workers.dsl
done
```

The startup `status` reply ends with `chunk_workers=configured,started,active`.
For a run with `--chunk-workers 4`, all three values should settle at `4,4,0`
after the stream wait; `auto` reports the detected worker count instead. A
nonzero `active` value in a trace sample means generation or meshing was still
using a worker at that frame, not that the configured pool was resized.

`--plugin-dir DIRECTORY` loads optional `.so` extensions after the window is
created. Files are validated and activated in deterministic priority/id order;
an unreadable directory, duplicate id, ABI mismatch, or callback rejection
ends startup with exit code `2`. The option is independent of the DSL and can
be combined with either debug entry point.

Compare the final trace samples with `jq`:

```sh
for workers in auto 2 4; do
  printf '%s ' "$workers"
  jq -r 'select(.stream != null) |
    [.stream.pending_gen, .stream.pending_mesh,
     .stream.generation_cpu_ms, .stream.mesh_cpu_ms,
     .timing.streaming_main_cpu_ms] | @tsv' \
    "/tmp/chunk-workers-$workers.jsonl" | tail -1
done
```

The columns are pending generation jobs, pending mesh jobs, cumulative
generation CPU time, cumulative mesh CPU time, and main-thread streaming CPU
time. Use the same `stream wait` timeout and compare several runs; lower worker
counts can be preferable when the last column or compositor responsiveness is
the limiting factor. Do not compare a run that failed `stream.surface_ready` or
that was started with a different seed/teleport position.

Every enabled run disables autosave and uses the fixed 60 FPS debug clock. On
startup the process writes a readiness line similar to:

```text
DEBUG_CONTROL ready mode=dsl plugins=0 commands=start,screenshot,status,pause,hud,world,stream,save,load,map,surface,marker,teleport,look,input,mouse,ship,view,fluid,water,weather,evolution,block,flora statements=let,assert,wait,repeat,exit
```

`--debug-trace [PATH]` can be combined with either debug entry point. Omitting
`PATH` selects the default trace location; an explicitly empty path or a path
of 512 bytes or longer is a startup error (exit code `2`) before a window is
created. Its JSONL
records include monotonic wall time as `elapsed_real_ms` and process CPU time
as `elapsed_cpu_ms`. `elapsed_main_cpu_ms` isolates the main game/render
thread from background generation and meshing. Comparing their deltas
distinguishes game work from display waits and frame limiting. Sample records
also split the current frame's main-thread work into `timing.update_main_cpu_ms`
and `timing.render_main_cpu_ms`. The update value is further divided into
`simulation_main_cpu_ms`, `streaming_main_cpu_ms`,
`interaction_main_cpu_ms`, and `environment_main_cpu_ms`.
Environment timing is further split into `astronomy_main_cpu_ms`,
`ecology_main_cpu_ms`, `sky_main_cpu_ms`, `water_main_cpu_ms`,
`environment_sample_main_cpu_ms`, and `environment_present_main_cpu_ms`.
The ecology value is split into `ecology_prepare_main_cpu_ms`,
`ecology_step_main_cpu_ms`, `flora_visuals_main_cpu_ms`, and
`entities_main_cpu_ms`; `simulation_steps` reports the number of fixed
simulation steps executed for that frame. Each sample's `stream` object also exposes cumulative
`generation_cpu_ms`, `mesh_cpu_ms`, and `upload_cpu_ms`, plus
`max_upload_cpu_ms`, so a main-thread streaming spike can be separated from
background terrain and mesh work.
Add `--debug-trace-frames` when a sample is needed for every playing frame;
the option also enables tracing and uses the default trace path unless
`--debug-trace PATH` is supplied. Frame records add
`timing.world_submit_main_cpu_ms`, `timing.shadow_main_cpu_ms`, and the CPU and
wall portions of `EndDrawing`. The `render` object reports the Raylib frame
interval, render dimensions, effective render distance, world candidate and
draw counters, and a nonblocking GPU world-pass query. `gpu_frame` identifies
the frame that produced `gpu_world_ms`, because GPU results can arrive a few
frames after submission. `visible_sections` counts instrumented visible
sections, not all chunks in the distance radius. Local surface terrain fades
into the active fog color at the configured square render-distance boundary.
Changing `[`/`]` changes both the streamed grid and this visibility limit;
space scenes keep their long-range visibility.
The `ecology_population` object splits the active-region update into key
preparation, storage admission, working-set construction, environment sampling,
population/evolution preparation, migration, and commit CPU time. It also
reports the speculative worker queue (`jobs_submitted`, `jobs_committed`,
`jobs_discarded`, and `synchronous_batches`) and its worker CPU time. A
successful prefetch makes the next step's main-thread sample and preparation
time near zero; validation rejects a job when the live records, world epoch,
seed, profile, or edit revision changed.

## Process and stdin lifetime

The final top-level statement determines whether a file is a batch:

| Source | Completion | Process behavior |
| --- | --- | --- |
| `--debug-script` file whose final statement is not `exit` | normal completion or DSL error | The executor stops and the game remains running. With `--debug-stdin`, the next block can be submitted. Without stdin, the window remains open until its normal close path. |
| `--debug-script` file whose final top-level statement is `exit` | `exit N` | The process exits with code `N` and does not hand off to stdin. |
| batch file with a parse/runtime/command/assert/wait failure | error | The process exits with code `3`. |
| stdin block ending in `exit [N]` | `exit N` | The process exits with code `N`. |
| stdin block with a syntax, assertion, timeout, or command failure | error | The current block is discarded, an error is reported, and the game remains running. |

`exit` is only valid at the top level and must be the final top-level
statement. `exit` with no expression means `exit 0`. `quit` is deliberately
rejected by the DSL; use `exit` so the exit code and batch semantics are
explicit. Invalid script options and unreadable script files fail startup with
exit code `2`.

When stdin is used, send one source line at a time. A `repeat` block remains
buffered until its matching `}` arrives. Closing stdin while a block is still
open reports an unterminated block; it does not silently execute a partial
script. The accumulated stdin block is limited to 16 KiB and individual input
lines to the debug control buffer size (2047 bytes of text).

## Source layout and statements

Source is line-oriented. Blank lines and lines whose first non-whitespace
character is `#` are ignored. A trailing semicolon is optional on every
statement. Inline comments are not special, so put comments on their own lines.
Commands occupy one line. A `repeat` body uses a brace on the `repeat` line and
a matching `}` line:

```text
repeat 2 {
  water debug on
  water debug off
}
```

The available statements are:

```text
let NAME = EXPRESSION
assert EXPRESSION
wait until EXPRESSION timeout EXPRESSION
repeat EXPRESSION {
  ...statements...
}
exit [EXPRESSION]
COMMAND [with ${EXPRESSION} interpolation]
```

`let` values are immutable. A name cannot be defined twice, including across
successive stdin blocks: the DSL environment belongs to the process and is
kept when a block completes. Use a new name or restart the process.

`assert` and `wait until` require a boolean expression. `wait until` evaluates
its condition once per debug frame and requires an integer timeout from 1 to
36,000 frames. A timeout is a DSL error. `repeat` requires an integer count from
0 to 3,600; zero skips the body. Nested repeats are limited to 32 levels. The
executor also limits a script to 1,000,000 executed statements.

## Values and expressions

The value types are:

* numbers: finite double-precision literals;
* booleans: `true` and `false`;
* strings: double-quoted, with `\\`, `\"`, `\n`, `\r`, and `\t` escapes;
* vectors: `[x, y, z]` or `vec3(x, y, z)`.

Vector components are accessed as `.x`, `.y`, and `.z`, for example
`player.position.y` or `origin.x`. Qualified runtime names such as
`world.dimension` are resolved as one name before an optional vector field.

Operators are listed from lowest to highest precedence:

```text
or, ||
and, &&
==, !=
<, <=, >, >=
+ , -
* , /, %
unary !, not, +, -
```

The spaces in the `+ , -` and `* , /, %` rows are only for readability; use
ordinary operators such as `a + b` and `a * b`. `and`/`&&` and `or`/`||` are
short-circuiting. Equality requires both operands to have the same type;
ordered comparisons require numbers. Numeric arithmetic supports numbers,
vector plus/minus vector, vector times scalar (in either order), and vector
divided by scalar. Division or remainder by zero, non-finite results, and
invalid type combinations are errors. Parentheses may group any expression;
expression nesting is limited to 64 levels.

Command interpolation evaluates an expression and formats it as follows:

```text
teleport ${origin.x} ${origin.y} ${origin.z} 3.141593 -0.25
marker add ${origin.x} ${origin.z} cyan seed-${world.seed}
```

Booleans format as `true`/`false`, numbers use a precise decimal form, strings
are inserted as-is, and vectors use `[x,y,z]`. The expanded command is limited
to 1,023 bytes. An undefined name, type error, or unterminated `${...}` aborts
the current DSL block.

## Runtime namespaces

Runtime values are sampled when an expression is evaluated:

| Name | Type | Meaning |
| --- | --- | --- |
| `game.screen` | string | `"start"` or `"playing"` |
| `game.paused` | bool | Whether the pause menu is open |
| `world.seed` | number | Current world seed |
| `world.dimension` | string | Current dimension name |
| `world.surface_body` | number | Canonical ID of the active solid body |
| `world.longitude` | number | Canonical longitude in radians |
| `world.latitude` | number | Canonical latitude in radians |
| `world.north_sign` | number | Current map chart's north tangent sign (`1` or `-1`) |
| `world.canonical_position` | vec3 | Player position with canonical surface X/Z coordinates |
| `world.loaded_canonical_chunks` | number | Unique loaded chunks on the active surface body |
| `world.duplicate_canonical_chunks` | number | Loaded chunks that duplicate an earlier canonical identity |
| `world.longitude_alias` | bool | Whole-longitude aliases resolve to the player's canonical cell |
| `world.north_pole_alias` | bool | North-pole reflection aliases resolve to the player's canonical cell |
| `world.south_pole_alias` | bool | South-pole reflection aliases resolve to the player's canonical cell |
| `world.last_rebase` | bool | A surface rebase has occurred since the current world reset |
| `world.last_rebase_from` | vec3 | Pre-rebase map X/Z, represented as `[x,0,z]` |
| `world.last_rebase_to` | vec3 | Canonical post-rebase map X/Z, represented as `[x,0,z]` |
| `world.last_rebase_north_sign` | number | North tangent sign used by the latest rebase, or `0` before one |
| `world.rebase_count` | number | Monotonic surface rebase sequence since the current world reset |
| `weather.canonical_cell` | vec3 | Canonical spherical weather sample cell (Y is the player Y) |
| `ecology.canonical_cell` | vec3 | Canonical spherical ecology cell (Y is the player Y) |
| `ecology.region_x`, `ecology.region_z` | number | Canonical 64-block ecology population region coordinates |
| `ecology.hot_regions`, `ecology.cold_regions`, `ecology.stored_regions` | number | Resident, evicted and total retained regional records; reading these fields does not activate or advance regions |
| `ecology.storage_bytes`, `ecology.storage_limit`, `ecology.failed_admissions` | number | Allocated regional storage bytes, total record limit and failed admissions (existing history is retained on failure) |
| `ecology.catchup_pending` | bool | Regional catch-up is pending; local simulation and edits wait for committed regional state |
| `evolution.region_available` | bool | Whether the current playing surface has a readable regional evolution snapshot |
| `evolution.lineage_count` | number | Active bounded lineages in the current ecology region (0 when unavailable) |
| `evolution.bootstrap_generation` | number | Regional evolution bootstrap generation, bounded to 24 (0 when unavailable) |
| `evolution.bootstrap_complete` | bool | Whether regional evolution bootstrap has reached generation 24 |
| `evolution.herbivore_density`, `evolution.omnivore_density`, `evolution.carnivore_density` | number | Current regional fauna density split by trophic class (0 when unavailable) |
| `player.position` | vec3 | Player world position |
| `player.velocity` | vec3 | Player velocity |
| `player.input_frames` | number | Remaining scripted player-input frames |
| `interaction.mouse_left_mesh_ready` | bool | The latest scripted left-click block edit has reached the uploaded section mesh |
| `interaction.mouse_left_press_frame` | number | Debug frame that queued the latest scripted left click |
| `interaction.mouse_left_handle_frame` | number | Debug frame that consumed the latest scripted left click |
| `interaction.mouse_left_set_block_frame` | number | Debug frame that committed the latest scripted block removal |
| `interaction.mouse_left_mesh_frame` | number | Debug frame that observed the rebuilt section mesh uploaded |
| `perf.enabled` | bool | Performance collection is enabled |
| `perf.route_complete` | bool | Warmup and frame sampling are complete |
| `perf.report_written` | bool | The performance report has been written |
| `perf.report_passed` | bool | The written report passed its configured baseline |
| `water.feet_submerged` | bool | Feet are in water |
| `water.body_submerged` | bool | Body is in water |
| `water.eyes_submerged` | bool | Eyes are in water |
| `water.surface_y` | number | Local water surface height |
| `stream.surface_ready` | bool | No missing surface chunks around the player |
| `stream.missing_surface_chunks` | number | Missing surface chunk count, or `-1` outside play |
| `stream.audit_complete` | bool | No stream audit or stream wait is active |
| `stream.chunks.status` | string | `valid` when the copied chunk snapshot is available, otherwise `unavailable` |
| `stream.chunks.capture_sequence` | number | Monotonic chunk-module snapshot sequence for the current process |
| `stream.chunks.capture_id` | number | Shared diagnostics observation ID for the current snapshot |
| `stream.chunks.frame_id` | number | Game frame associated with the current snapshot, or `0` when unavailable |
| `stream.chunks.simulation_tick` | number | Simulation tick associated with the current snapshot, or `0` when unavailable |
| `stream.chunks.world_generation` | number | World context generation associated with the current snapshot, or `0` when unavailable |
| `stream.chunks.blocked_reason` | string | Current queue state: `none`, `generation_pending`, `mesh_pending`, `generation_queue_full`, `mesh_queue_full`, `generation_workers_unavailable`, `mesh_workers_unavailable`, `generation_apply_pending`, or `mesh_upload_pending` |
| `stream.chunks.last_event_reason`, `stream.chunks.last_event_sequence` | string, number | Last scheduler event and its sequence, retained after work completes; resets with streaming stats |
| `stream.chunks.last_event_time_ms` | number | Event's monotonic process-clock time in milliseconds, or zero before any event |
| `stream.chunks.last_event_has_location`, `stream.chunks.last_event_has_section` | bool | Whether the event identifies a chunk and an individual vertical section |
| `stream.chunks.last_event_location` | vec3 | Chunk X, section Y, chunk Z; check the location/section flags before interpreting coordinates |
| `stream.chunks.blocked_reason_sequence` | number | Deprecated alias of `last_event_sequence`; does not identify the current queue state |
| `stream.chunks.completed_generation_jobs`, `stream.chunks.completed_mesh_jobs` | number | Completed jobs waiting for main-thread application or upload |
| `stream.chunks.lod_consistency` | string | `not_captured` for base snapshots, or `consistent` when LOD counters were copied under the scheduler lock |
| `stream.chunks.active_chunks` | number | Active chunks copied into the current snapshot |
| `stream.chunks.pending_generation_jobs`, `stream.chunks.pending_mesh_jobs` | number | Generation and mesh jobs observed in the current snapshot |
| `stream.chunks.focus_chunk_loaded` | bool | Whether the streaming focus chunk was loaded at capture time |
| `stream.chunks.generation_submitted`, `stream.chunks.generation_completed` | number | Cumulative generation jobs in the current snapshot |
| `stream.chunks.mesh_submitted`, `stream.chunks.mesh_completed` | number | Cumulative mesh jobs in the current snapshot |
| `stream.chunks.upload_budget_deferrals` | number | Cumulative mesh uploads deferred by the frame budget |
| `stream.lod_coarse_allowed` | bool | Coarse chunk LODs are allowed in the current world state |
| `stream.lod_target_changes` | number | Cumulative chunk target LOD changes since the current world reset |
| `stream.lod_target_exact`, `stream.lod_target_half`, `stream.lod_target_quarter` | number | Loaded chunks targeting exact, half-resolution, or quarter-resolution meshes |
| `stream.lod_active_exact`, `stream.lod_active_half`, `stream.lod_active_quarter` | number | Loaded chunks currently rendering exact, half-resolution, or quarter-resolution meshes |
| `stream.lod_ready_exact_sections`, `stream.lod_ready_half_sections`, `stream.lod_ready_quarter_sections` | number | Section mesh caches ready for the current data version at each LOD |
| `stream.lod_proxy_flora_half`, `stream.lod_proxy_flora_quarter` | number | Ready half- or quarter-resolution section tree proxy meshes |
| `stream.lod_jobs_exact`, `stream.lod_jobs_half`, `stream.lod_jobs_quarter` | number | Queued, running, or completed mesh jobs awaiting retirement at each LOD |
| `stream.workers_configured` | number | Target surface chunk worker count |
| `stream.workers_started` | number | Successfully started surface chunk workers |
| `stream.workers_active` | number | Surface chunk workers currently processing jobs |
| `fluid.volume` | number | Fluid volume at the player |
| `fluid.surface_y` | number | Fluid surface at the player |
| `fluid.queue_overflows` | number | Fluid solver queue overflow count |
| `weather.climate` | string | Local named climate regime, or `"unavailable"` outside a surface world |
| `atmosphere.valid`, `atmosphere.active` | bool | Column sample is valid / contains air at the player's height |
| `atmosphere.layer` | string | `Vacuum`, `Troposphere`, `Stratosphere`, `Mesosphere`, `Thermosphere`, or `Exosphere` |
| `atmosphere.altitude_m` | number | Physical reference altitude in metres, after game-height conversion |
| `atmosphere.temperature_k` | number | Local kinetic temperature in kelvin; zero in vacuum |
| `atmosphere.pressure_atm` | number | Local pressure in atmospheres |
| `atmosphere.density_kg_m3`, `atmosphere.density_ratio` | number | Local mass density / density relative to this column's reference surface |
| `atmosphere.column_fraction` | number | Remaining pressure fraction above the observer (0-1) |
| `atmosphere.molar_mass_kg_mol` | number | Mean molar mass in kg/mol |
| `atmosphere.ozone`, `atmosphere.ionization` | number | Normalized absorbing-layer / upper-air ionization proxies |
| `weather.phenomenon` | string | Dominant current weather phenomenon |
| `weather.cloud_genus` | string | Dominant WMO cloud genus, or `None` |
| `weather.cloud_cover` | number | Aggregate cloud cover from 0 to 1 |
| `weather.cloud_flags` | number | Bit mask of all WMO cloud genera currently present |
| `weather.cloud_layers` | number | Number of bounded renderer cloud layers (0-3) |
| `weather.cloud_reference_y` | number | Fixed cloud datum: active surface sea level, or Y=12 on a dry/flat world; 0 outside a valid atmosphere |
| `weather.cloud_bottom_y` | number | Lowest selected cloud layer's world-space base (datum plus layer base); 0 outside a valid atmosphere |
| `weather.cloud_forced_frames` | number | Remaining forced-cloud update frames |
| `weather.temperature`, `weather.temperature_k` | number | Local air temperature in kelvin |
| `radiation.active` | bool | Whether the fixed-step radiation column is valid for the player's active surface region |
| `radiation.temperature_k` | number | Integrated radiation-column surface temperature in kelvin, or `0` when inactive |
| `radiation.baseline_temperature_k` | number | Climate temperature used when the current radiation region was bound, or `0` when inactive |
| `radiation.net_flux_wm2` | number | Latest net radiative flux at the column surface in W/m^2, or `0` when inactive |
| `radiation.equilibrium_temperature_k` | number | Instantaneous radiative equilibrium temperature in kelvin, or `0` when inactive |
| `radiation.step_count` | number | Fixed radiation integration steps completed for the current region, or `0` when inactive |
| `radiation.region_x`, `radiation.region_z` | number | Canonical 128-block radiation region containing the player; both are `-1` outside an active surface |
| `hydrology.active` | bool | Whether the bounded 128-block hydrology region is active for the player's surface cell |
| `hydrology.region_x`, `hydrology.region_z` | number | Canonical 128-block hydrology region containing the player; both are `-1` outside an active surface |
| `hydrology.player_region_x`, `hydrology.player_region_z` | number | Region implied by the player's canonical position while the surface is active |
| `hydrology.step_count` | number | Fixed hydrology solver steps completed for the current region |
| `hydrology.weather_ready` | bool | Whether the latest full 32x32 precipitation input pass is complete |
| `hydrology.weather_uniform` | bool | Whether the current precipitation input came from a spatially uniform forced/manual weather override |
| `hydrology.weather_samples` | number | Weather cells sampled during the latest simulation step |
| `hydrology.weather_reused_cells` | number | Cells still using the previous precipitation input during the latest step |
| `hydrology.weather_pending` | number | Cells remaining in the current precipitation refresh pass |
| `hydrology.weather_cursor` | number | Next coarse-grid cell index in the precipitation refresh pass |
| `hydrology.weather_passes` | number | Completed precipitation refresh passes for the active region |
| `hydrology.terrain_samples` | number | Terrain cells refreshed during the latest simulation step |
| `hydrology.terrain_pending` | number | Terrain cells remaining after an edit-triggered refresh pass |
| `hydrology.inputs_main_cpu_ms` | number | Main-thread time spent binding and refreshing hydrology inputs in the latest simulation step |
| `hydrology.solver_main_cpu_ms` | number | Main-thread time spent advancing the hydrology solver in the latest simulation step |
| `hydrology.total_water_m3` | number | Total surface, soil, and groundwater volume in the active coarse grid |
| `hydrology.total_sediment_kg` | number | Total suspended sediment mass in the active coarse grid |
| `hydrology.surface_water`, `hydrology.surface_water_m` | number | Surface water depth at the player's coarse cell, in metres |
| `hydrology.soil_moisture` | number | Bounded soil moisture feedback fraction from 0 to 1 |
| `hydrology.groundwater`, `hydrology.groundwater_m` | number | Groundwater depth at the player's coarse cell, in metres |
| `hydrology.runoff`, `hydrology.runoff_m3_per_s` | number | Runoff discharge from the player's coarse cell, in cubic metres per second |
| `hydrology.evaporation`, `hydrology.evaporation_m_per_s` | number | Configured evaporation rate, in metres per second |
| `hydrology.sediment`, `hydrology.sediment_kg_m2` | number | Sediment load at the player's coarse cell, in kilograms per square metre |
| `weather.pressure`, `weather.pressure_atm` | number | Surface pressure in atmospheres |
| `weather.humidity`, `weather.relative_humidity` | number | Relative humidity from 0 to 1 |
| `weather.dew_point`, `weather.dew_point_k` | number | Dew point in kelvin |
| `weather.wet_bulb`, `weather.wet_bulb_k` | number | Wet-bulb temperature in kelvin |
| `weather.wind`, `weather.gust`, `weather.visibility` | number | Normalized wind, gust, and visibility values |
| `weather.rain`, `weather.snow`, `weather.sleet` | number | Normalized precipitation-phase intensities |
| `weather.freezing_rain`, `weather.hail` | number | Normalized freezing-rain and hail intensities |
| `weather.lightning`, `weather.fog`, `weather.dust` | number | Normalized phenomenon intensities |
| `weather.rainbow`, `weather.aurora` | number | Normalized optical-phenomenon intensities |
| `weather.surface_count` | number | Tracked weather-affected surface count |
| `weather.active_fires` | number | Active weather fire count |
| `weather.fire_phase` | string | Nearest loaded fire phase: `inactive`, `igniting`, `flaming`, or `smoldering` |
| `weather.fire_position` | vec3 | Nearest loaded fire block position, or `[0,0,0]` when none is active |
| `weather.fire_distance` | number | Distance to the nearest loaded fire, or `-1` when none is active |
| `weather.fire_intensity` | number | Nearest fire combustion intensity from 0 to 1 |
| `weather.fire_fuel` | number | Nearest fire remaining modeled fuel load |
| `weather.fire_moisture` | number | Nearest fire fuel moisture from 0 to 1 |
| `weather.fire_local_heat` | number | Sheltered/submerged player heat exposure from all loaded fires |
| `weather.fire_local_smoke` | number | Sheltered/submerged player smoke exposure from all loaded fires |
| `weather.fire_ignitions` | number | Cumulative accepted ignitions, including spread |
| `weather.fire_spread_ignitions` | number | Cumulative ignitions caused by fire spread |
| `weather.fire_extinctions` | number | Cumulative fires that reached extinction |
| `weather.fire_suppressions` | number | Cumulative fire records reached by suppression |
| `weather.fire_burned_blocks` | number | Cumulative blocks consumed by fire, excluding other weather damage |
| `weather.fire_burn_sites` | number | Active persistent burn-recovery records |
| `weather.fire_recovered_sites` | number | Cumulative burn records that fully recovered |
| `weather.fire_dropped_ignitions` | number | Ignitions rejected at the bounded active-fire limit |
| `weather.fire_dropped_burn_sites` | number | Burn records dropped at the bounded recovery-record limit |
| `weather.tornado_active` | bool | Whether a local tornado is active |
| `weather.tornado_forced` | bool | Whether the active tornado came from debug forcing |
| `weather.tornado_phase` | string | `inactive`, `forming`, `intensifying`, `mature`, or `dissipating` |
| `weather.tornado_center` | vec3 | Current moving funnel-base position |
| `weather.tornado_distance` | number | Horizontal player distance, or `-1` when inactive |
| `weather.tornado_intensity` | number | Current lifecycle-scaled intensity from 0 to 1 |
| `weather.tornado_radius` | number | Core vortex radius in world units |
| `weather.tornado_funnel_height` | number | Condensation-funnel height in world units |
| `weather.tornado_wind_mps` | number | Maximum modeled tangential wind in meters per second |
| `weather.tornado_forced_frames` | number | Remaining forced-tornado update frames |
| `weather.tornado_blocks_damaged` | number | Cumulative tornado block removals this session |
| `weather.tornado_debris_emitted` | number | Cumulative block-debris particles emitted this session |
| `weather.tornado_dropped_effects` | number | Tornado effects dropped at the bounded effect-queue limit |
| `weather.forced_frames` | number | Remaining forced-weather update frames |
| `weather.damage_enabled` | bool | Environmental weather effects are enabled |
| `target.hit` | bool | Current solid raycast hit exists |
| `target.position` | vec3 | Current raycast block position; only meaningful when `target.hit` is true |
| `target.block` | string | Target block name, or `air` when there is no hit |
| `render.monitor_refresh_hz` | number | Current monitor refresh rate reported by the window backend |
| `render.vsync` | bool | Whether the window backend accepted the VSync window flag |
| `block.catalog_count` | number | All ordinary/natural and color block identities |
| `block.natural_count` | number | Block identities in the natural range, including Stage 06 flora |
| `block.stage05_count` | number | Geological, soil, biogenic, and fire-residue identities added in Stage 05 |
| `block.gallery_active` | bool | Whether a Stage 05 gallery was successfully placed in this process |
| `block.gallery_origin` | vec3 | Origin of the latest successful block gallery |
| `block.gallery_placed` | number | Blocks placed by the latest successful gallery command |
| `block.gallery_rows` | number | Deterministic family rows in the gallery |
| `flora.catalog_count` | number | Real-world Homeworld taxa in the Stage 06 catalog |
| `flora.sample_tree` | string | Deterministic tree taxon at the latest `flora sample`, or `none` |
| `flora.sample_ground` | string | Deterministic understory taxon at the latest sample, or `none` |
| `flora.sample_burn_stage` | string | Fire-recovery succession stage at the latest sample |
| `flora.sample_biome` | string | Homeworld biome at the latest sample |
| `flora.sample_substrate` | string | Soil or surface substrate at the latest sample |
| `flora.sample_habitat` | vec3 | Temperature in K, moisture, and usable light at the latest sample |
| `flora.sample_temperature` | number | Temperature in K at the latest sample |
| `flora.sample_moisture` | number | Moisture fraction at the latest sample |
| `flora.gallery_active` | bool | Whether a Stage 06 flora gallery was successfully placed |
| `flora.gallery_origin` | vec3 | Origin of the latest successful flora gallery |
| `flora.gallery_placed` | number | Blocks placed by the latest flora gallery command |
| `flora.gallery_trees` | number | Tree specimens in the latest flora gallery |
| `flora.gallery_ground` | number | Ground-plant specimens in the latest flora gallery |
| `ship.driving` | bool | Player is driving a ship |
| `ship.mode` | string | Current ship drive mode |
| `render.water_debug` | bool | Water section bounds are enabled |
| `render.water_debug_through` | bool | Water bounds draw through terrain |
| `render.water_triangles` | number | Water debug pass triangle count |
| `settings.autosave` | bool | Autosave state (false in debug mode) |

## Command catalog

These are the commonly used command forms. Commands are dispatched through the
same debug control implementation used by the test harness, so a command can
also be rejected by the current game state (for example, `teleport` before
`start`). A rejected or failed command aborts the current DSL block.

Base chunk diagnostic fields are captured once per DSL observation point and
reused by its assertions. They never scan LOD sections; their
`stream.chunks.lod_consistency` is `not_captured`. The first `stream.lod_*`
query takes a separate detailed snapshot, reused by subsequent LOD queries.
It does not replace the base snapshot. `stream.lod_capture_id`,
`stream.lod_frame_id`, `stream.lod_simulation_tick`, and
`stream.lod_world_generation` identify that detailed observation, independently
of the corresponding `stream.chunks.*` fields. Both groups refresh after a
frame advance, world-context change, or successful debug command (also after
a dispatched command reports failure). Unrelated fields do not capture LOD.

`make test-chunk-diagnostics-e2e` exercises these contracts in a real Hyprland
game process: stable repeated reads, independent lazy LOD capture, command
and frame invalidation, and Homeworld/planet transitions. It uses an isolated
temporary save/settings directory and preserves its DSL and log on failure.
The same route loads a test plugin against both stream service versions and
checks screenshot/report capture identity. It requires `uv` for report checks.

`blocked_reason` summarizes current queued work, independently of history.
Completed meshes and generation results take precedence because the main
thread can drain them, followed by unavailable workers, full queues, and
pending generation/mesh work. `none` does not certify local terrain readiness;
use `stream wait` or `stream.surface_ready` for that. `last_event_*` retains
budget deferrals and failures even after queues drain. Its time is a monotonic
wall-clock timestamp, and locations use chunk/section indices, not blocks.

### Session, rendering, and world

```text
start
terrain flat|varied
status
pause on|off
hud debug on|off
world topology
screenshot
save
load
map
map layer liquids on|off
surface debug home
surface debug planet temperate|desert|ice|lava|crater SEED
water debug
water debug on|off
water debug through
water debug through on|off
view first|third
```

`water debug` and `water debug through` toggle their current setting when the
`on`/`off` argument is omitted. `start` is valid on the start screen.
`terrain flat` and `terrain varied` select the terrain used by the next
`start`; both commands are valid only on the start screen.
`world topology` is a read-only surface-world inspection. It reports the body,
canonical X/Z and longitude/latitude, north tangent sign, longitude and pole
alias checks, unique and duplicate canonical chunk counts, and the most recent
surface rebase event.
`screenshot` first reports
`DEBUG_CONTROL screenshot scheduled`; after the frame capture it reports
`DEBUG_CONTROL capture ok png=PATH report=PATH`. The report is a key/value text
file next to the PNG. Debug-controlled sessions start with the help overlay
hidden so visual captures remain unobstructed. `map` toggles the surface map
and requires an active surface world. `map layer liquids` controls the
map's water-cave/lava-cavity
overlay. `surface debug home` switches directly to Homeworld; the planet form
creates a deterministic solid surface for the requested style and seed. These
surface commands use normal chunk teardown and surface activation, and require
the playing screen. `save` and `load` require the playing screen.

`map`, `map layer liquids`, `view`, and `look` report their resulting state in
their `DEBUG_CONTROL` reply; they do not currently expose dedicated typed
resolver fields. Use the reply for a command contract assertion, then use
`status` when a frame-level snapshot is needed (`third_person` is included
there). `surface debug` reports the active world and seed and should be
followed by `wait until stream.surface_ready` before querying the new surface.

While a surface is active, the hydrology bridge samples the same canonical
128-block region as the weather and radiation runtimes. A useful readiness
check is `wait until hydrology.active timeout 300`; outside a surface (or in
the Nether) all hydrology fields return inactive defaults. The runtime is a
32x32 coarse grid (4 metres per cell), so its values describe catchment-scale
water and sediment rather than individual voxel-fluid blocks.

The map's `GLOBAL SURFACE` globe can be left-dragged to rotate. Clicking a
visible point focuses the regional map on that spherical location; clicking a
nearby custom marker also toggles its navigation target. The globe keeps its
rendered surface cached while only the camera and marker state are unchanged.

### Streaming

```text
stream wait [FRAMES]
stream audit [RADIUS]
stream audit at X Y Z [RADIUS]
```

`stream wait` defaults to 300 frames and accepts 1-3,600. It records the
player's starting chunk and section, then waits for the surrounding 3x3x3
section region to reach the `ready` or `implicit` pipeline stage for two
consecutive frames. Global generation/mesh queue counts are diagnostics only.
The start and result replies look like:

```text
DEBUG_CONTROL stream wait started timeout_frames=300
DEBUG_CONTROL stream wait result=settled elapsed_frames=... focus=... pending_local_sections=0 first_pending=... pending_stages=missing:...,gen_wait:...,gen:...,dirty:...,mesh:... pending_gen=... pending_mesh=... pending_mesh_snapshot_bytes=... missing_surface_chunks=...
```

If the local region is not settled by the timeout, the result is
`result=timeout`, the current DSL executor is aborted with a timeout error, and
the rest of that block is not executed. `first_pending` identifies the first
unsettled section, while `pending_stages` splits the count by missing section,
generation wait, active generation, dirty mesh, and active mesh work. The wait
actively requests its fixed starting 3-by-3-by-3 section window, can only run
while playing in a surface world, and cannot overlap an audit.
`stream audit` scans a radius
around the player (default 2, range 1-4), or an explicit block coordinate; its
result includes issue counts and can report `stale_rerun_required` when chunks
changed during the scan.

### Movement and input

```text
teleport X Y Z YAW PITCH
look YAW PITCH
look delta YAW_DELTA PITCH_DELTA
input FORWARD STRAFE VERTICAL SPRINT FRAMES
mouse left
mouse right
```

`pause on` opens the ordinary pause menu while playing, and `pause off`
closes it. This is useful for deterministic menu screenshots and assertions.

`hud debug on` and `hud debug off` toggle the F3 diagnostics overlay while
playing. Enabling it closes the F1 help panel, matching the keyboard shortcut.
Use `hud debug on` followed by `screenshot` to capture a readable diagnostic
frame without synthetic key input.

Movement components are in `[-1,1]`; `SPRINT` is `0` or `1`; scripted input
lasts 1-600 frames. Teleport coordinates are finite and bounded to +/-1,000,000,
yaw to +/-1,000, and pitch to [-1.45, 1.45]. Absolute look uses the same pitch
range; relative look deltas are bounded to +/-1,000 and the resulting pitch is
clamped.
`mouse left` and `mouse right` inject one button-press edge through their normal
interaction paths. For `mouse left`, the `status` reply includes the debug frame
at which the edge was queued, handled, committed with `SetBlock`, and observed
in the uploaded section mesh. The typed fields
`interaction.mouse_left_press_frame`, `interaction.mouse_left_handle_frame`,
`interaction.mouse_left_set_block_frame`, `interaction.mouse_left_mesh_frame`,
and `interaction.mouse_left_mesh_ready` expose the same lifecycle for DSL
assertions; `mouse right` is currently contract-tested by its reply only.

### Fluids

```text
fluid inspect
fluid inspect X Y Z
fluid set X Y Z VOLUME
fluid step TICKS
```

`fluid inspect` without coordinates samples the player cell. `VOLUME` is
0-255; `TICKS` is 1-1,000,000. `fluid set` and `fluid step` require an active
surface world.

Fluid mutations also require a generated terrain section. An absent section
is treated as unknown until terrain generation confirms its contents; generated
empty sections can receive water without allocating block storage in advance.
Edited water at a section boundary resumes flowing when the adjacent section
finishes loading. Saved water is applied after that section's terrain baseline.

The submerged water-wall regression uses the original position and viewing
direction, activates water above an ungenerated section, then descends and
repeats the check after save/load:

```sh
make
bash tests/test_water_section_e2e.sh build/normal/voxelcraft
# Optional single-worker run:
WATER_SECTION_WORKERS=1 bash tests/test_water_section_e2e.sh
```

The script keeps its screenshots, reports and trace in the printed temporary
directory. Both captures must show full local water volume and zero internal
water triangles in the player's section.

### Block catalog and gallery

```text
block inspect NAME_OR_ID
block set X Y Z NAME_OR_ID
block gallery X Y Z
```

`block inspect` accepts a stable numeric ID or a canonical block name;
spaces, hyphens, underscores, and letter case are equivalent. It reports the
resolved ID and name, face textures, render shape, collision, translucency,
material response, and Stage 05 membership. `block set` uses the ordinary
surface edit and persistence path, requires the target section to be loaded,
and reports both the submitted coordinate and its canonical spherical cell.
It is intended for deterministic seam/pole and save/load checks. `block gallery` requires an active
surface world and a fully loaded 14-by-3 region beginning at `X Y Z`. It first
validates the entire bounded region, then places all 26 Stage 05 blocks in
geology, biogenic, and fire-residue rows as one undo group. A failed validation
or mutation leaves no partial gallery.

### Flora catalog, sampling, and gallery

```text
flora inspect NAME_OR_ID
flora sample X Z
flora gallery X Y Z
```

`flora inspect` accepts a numeric taxon ID or a common/scientific name, with
spaces, hyphens, underscores, and letter case treated equivalently. It reports
the common name, scientific name, family, growth form, succession stage,
temperature/moisture/light niche, elevation and slope limits, size, wind and
fire traits, and the primary/accent blocks. The 13 Earth taxa are used only on
Homeworld; alien planets retain their exobiological archetypes.

`flora sample X Z` evaluates the deterministic Homeworld habitat at a column,
including biome, substrate, climate, slope, fire-recovery stage, selected tree,
and understory taxon. It does not mutate the world. `flora gallery X Y Z`
requires an active surface world, an accessible Y coordinate, and a fully
loaded, empty bounded region. It validates every trunk, canopy, and understory
cell before placing six distinct tree forms and seven ground plants as one
undo group. A failed validation or mutation leaves no partial flora gallery.

### Atmospheric layers

`atmosphere.*` samples the player's current height. `weather.temperature_k`
and `weather.pressure_atm` remain the regional **surface** weather sample.
Outside a surface scene, in the Nether, or above the simulated exosphere,
atmospheric pressure/density are zero and the layer is `Vacuum`.

The physical column uses metres and hydrostatic pressure. Terrain coordinates
keep one metre per voxel above reference Y=12. Above Y=274, the upper column
is compressed into the existing launch corridor; on Homeworld, the five layer
bases are Y=12, 288, 302, 312.5, and 333.5, with vacuum at Y=344. Planet
corridors depend on their existing atmosphere depth. These heights are
presentation coordinates, not physical kilometre values.

A reproducible Hyprland check covering all five layers, ascent/descent and
screenshots is available with:

```bash
make
bash tests/test_atmosphere_e2e.sh build/normal/voxelcraft
```

For manual inspection, use the project DSL, for example:

```text
start
pause on
teleport 0 295 0 0 0
assert atmosphere.layer == "Stratosphere"
assert atmosphere.pressure_atm < 0.25
assert atmosphere.density_kg_m3 > 0
```

See [atmosphere-model.md](atmosphere-model.md) for the physical assumptions,
planet composition rules, and the boundary between column physics and the
renderer.

### Weather and climate

```text
weather inspect
weather force PHENOMENON INTENSITY FRAMES
weather cloud GENUS COVERAGE FRAMES
weather cloud clear
weather tornado force INTENSITY FRAMES [DISTANCE]
weather tornado clear
weather fire ignite X Y Z INTENSITY
weather fire suppress X Y Z RADIUS [AMOUNT]
weather fire clear
weather clear
weather damage on|off
weather step TICKS
```

`weather inspect` reports the local climate regime, temperature, pressure,
humidity, wind, visibility, all precipitation phases, optical phenomena, and
environmental-effect counts. `weather force` bypasses natural eligibility and
rarity for deterministic testing. `INTENSITY` is 0-1 and `FRAMES` is
1-36,000. Multiword phenomena use hyphens or underscores, for example
`heavy-rain`, `freezing_rain`, `strong-wind`, `dust-storm`, `heat-wave`, and
`cold-snap`. The complete set is `clear`, `cloudy`, `fog`, `frost`, `drizzle`,
`showers`, `heavy-rain`, `thunderstorm`, `lightning`, `sleet`,
`freezing-rain`, `hail`, `snow`, `blizzard`, `strong-wind`, `dust-storm`,
`heat-wave`, `cold-snap`, `rainbow`, and `aurora`.

The `radiation.*` values expose the bounded radiation column that feeds the
local climate sample. It advances on the same fixed simulation clock as the
rest of the world, and is rebound whenever the active surface or canonical
128-block region changes. `radiation.active` is false until the first valid
column step for that binding; while inactive, temperatures, flux, equilibrium,
and step count resolve to `0`, and `radiation.region_x/z` resolve to `-1` when
there is no active surface. Space, Nether, and other non-surface contexts do
not retain or expose the previous surface column.

Forced weather is runtime-only and is never saved. `weather clear` returns to
the natural field and clears separately forced cloud and tornado state.
`weather cloud` forces
one WMO cloud genus without replacing a forced weather phenomenon; `COVERAGE`
is 0-1, `FRAMES` is 1-36,000, and `weather cloud clear` restores natural cloud
classification. The accepted genera are `cirrus`, `cirrocumulus`,
`cirrostratus`, `altocumulus`, `altostratus`, `nimbostratus`,
`stratocumulus`, `stratus`, `cumulus`, and `cumulonimbus`. Cloud forcing is
also runtime-only. `weather inspect` reports the dominant and present genera,
the selected renderer layers, each layer's coverage, base, and thickness, and
`cloud_reference_y`. Layer bases are offsets above this fixed datum. The datum
does not follow terrain under the camera, so climbing a mountain can enter or
rise above clouds. Natural cloud bases can still change with weather.
`make test-cloud-altitude-e2e` verifies the fixed datum through this DSL on
Wayland and retains its screenshots, replies, and trace for visual review.

`weather tornado force` creates a deterministic tornado downwind of the player
without weakening natural formation thresholds. `INTENSITY` is 0-1, `FRAMES`
is 1-36,000, and optional `DISTANCE` is 8-160 world units (default 48). The
forced event uses the normal forming, intensifying, mature, and dissipating
lifecycle as well as the normal force, terrain, debris, funnel, and audio
paths. `weather tornado clear` removes only the tornado, preserving separately
forced weather and cloud state. Tornado state is runtime-only and is not saved.
`weather inspect` reports its phase, center, distance, intensity, radius,
funnel height, maximum wind, remaining frames, damage, and debris counters.

`weather fire ignite` applies a debug ignition source to a loaded flammable
block. `INTENSITY` is greater than 0 and at most 1; the normal fuel-moisture,
flammability, saturation, and active-fire-cap checks still apply. Repeating the
command at an already burning cell strengthens that fire without creating a
duplicate record. `weather fire suppress` cools loaded fires within a spherical
`RADIUS` of 0-64 world units. `AMOUNT` is greater than 0 and at most 1, defaults
to 1, and uses the same continuous moisture/cooling path as rain and nearby
water. A valid suppression with no fire in range succeeds with `affected=0`.
`weather fire clear` removes active fires while retaining burn history and all
forced weather, cloud, and tornado state. Inspect and typed fields expose the
nearest fire, local exposure, lifecycle counters, burned blocks, recovery
records, and bounded-drop counters. Screenshot report schema 12 records those
values plus the same-frame fire snapshot count, plume wind, haze, and graphics
quality budgets used for rendering, as well as the latest flora sample and
gallery state.

`weather damage off` restores reversible weather-owned
snow/ice and clears active weather fires; it changes the current game setting,
but debug runs do not save settings on exit. `weather step` advances
environmental effects by 1-100,000 fixed 2 Hz ticks without advancing world
time. Each affected column uses its own weather at that current world time;
forced/manual weather applies uniformly. Inspect, force, and step require an
active surface world.

Each impact tick advances every existing record on the active surface: up to
1,024 surface records, 256 loaded fires, and 512 burn-recovery records. These
capacity limits bound the work without slowing the simulation as records fill
up. New surface discovery remains limited to eight columns per tick. Newly
spread fires start advancing on the following tick. Unloaded fires and records
on another surface remain paused; unloaded weather-owned snow/ice retains its
ownership until its melted block can be restored safely. See
[weather simulation](weather-simulation.md) for clock and persistence details.

### Geology, ash layers and lava products (G1–G3 / P0)

```text
geology inspect
geology vents
geology force eruption INTENSITY TICKS
geology stop ID
geology step TICKS
geology deposit ash|basalt|silicic|scoria|pumice X Y Z UNITS
geology inventory ash|basalt|obsidian|scoria|pumice|rhyolite BLOCKS
```

G2 adds finite lava inventory, 10 Hz lava cells, thermal cooling, partial solid
overlays and full-cell basalt conversion. G3 adds fixed-step water contact: finite
water is atomically consumed only after a lava energy parcel commits; procedural
ocean cells remain full and count external reservoir supply. Replies include `lava_physics=1`. The red vent, ash column, sparks and
lava cells are drawn from the same saved state without consuming physics randomness.

`force` uses the canonical column 24 blocks east of the player, at procedural
surface height + 1. That column and its supporting block must be loaded and the
underlying vent cell must be air. It creates or reuses the stable vent at that address and
starts `unrest`; an already running event is rejected. `INTENSITY` is (0,1] and
`TICKS` is 1–36,000 eruption-phase ticks, excluding the prelude. An intensity of
1 has a 60-tick prelude; lower intensities take up to 180 ticks. Each tick is
0.5 active simulation seconds. `stop` accepts the 16-digit hexadecimal string
ID and enters cooling, retaining identity and event history. A stopped or
finished event can be started again after it reaches recovery.

`step` accepts 1–100,000 geology ticks, does not advance world time, and works
while paused. It only advances admitted, loaded vents within the active radius;
it never catches up parked vents. There are at most two active and 64 retained
vent records. Admission uses a 160-block radius and existing activity remains
until 224 blocks, measured along the surface plus height difference. Unloaded,
Nether/space and other-body vents are parked. A blocked outlet retains its
inventory while previously emitted lava can still cool. Inspect/vents/typed reads
do not discover or advance anything. Runtime discovery checks eligible dry
planetary volcanic peaks every two active seconds; Homeworld is debug-only.

The current world save V28 includes versioned `VOLCANO1`, `LAVA0003`, `ASH00002`
and `INVFR001` payloads with explicit little-endian scalar encoding, event state, material markers,
ash erosion credits, collection counters, six inventory remainders and fractional
step remainder. V27 reads `ASH00001` with zero collected ash; V18–V27 reset inventory
remainders without consuming trailing data. V26 reads `LAVA0002` with
zero material markers; V25 reads `LAVA0001` with zero reaction and material state.
V24 keeps volcano records with empty lava; V18–V23 reset both. V18–V26 start with
empty ash layers. New worlds reset all three runtimes;
surface transitions park records instead of deleting them.

| Field | Type | Meaning |
| --- | --- | --- |
| `geology.present`, `geology.active` | bool | Nearest current-body vent exists / was admitted on the latest geology update |
| `geology.id` | string | Stable 16-digit hexadecimal vent ID, or all zeroes |
| `geology.position` | vec3 | Nearest vent's canonical map X/Z and world Y |
| `geology.phase` | string | `none`, `dormant`, `unrest`, `erupting`, `waning`, `cooling`, or `recovery` |
| `geology.event`, `geology.ticks` | number | Event sequence and cumulative active ticks for that vent |
| `geology.pressure`, `geology.reservoir` | number | Normalized pressure and remaining magma inventory in block units |
| `geology.records`, `geology.active_count` | number | Retained records and admitted vents across runtime |
| `geology.rejected` | number | Admissions rejected at the storage cap |
| `geology.lava_cells`, `geology.hot_cells` | number | Retained lava records and current hot cells |
| `geology.lava_volume`, `geology.liquid_volume`, `geology.solid_volume` | number | Total, liquid and solid partial/full block volume |
| `geology.supplied`, `geology.solidified` | number | Emitted and full-cell solidified volume |
| `geology.volume_error`, `geology.energy_error` | number | Conservation residuals; should remain near zero |
| `geology.evaporated_units`, `geology.reservoir_units`, `geology.water_energy` | number | Committed water parcels, external reservoir supply and lava heat spent |
| `geology.evaporated_water` | number | Committed water vaporization in block units (255 parcels per block) |
| `geology.cell_contact_credit`, `geology.cell_steam` | number | Unsatisfied face credit and bounded recent steam visual strength |
| `geology.lava_ticks`, `geology.blocked_transfers`, `geology.rejected_cells` | number | Fixed-step and bounded-flow diagnostics |
| `geology.cell_volume`, `geology.cell_liquid`, `geology.cell_temperature` | number | Lava overlay at the player cell |
| `geology.cell_collision_height` | number | Actual solid height used by collision, including ash |
| `geology.ash_cells`, `geology.ash_ticks` | number | Retained ash cells and fixed 10 Hz steps |
| `geology.ash_volume`, `geology.ash_supplied`, `geology.ash_washed` | number | Remaining, supplied and rain-exported ash in block units |
| `geology.ash_volume_error` | number | Supplied minus remaining minus washed minus collected; must be zero |
| `geology.ash_collected`, `geology.collected` | number | Ash / cold lava-overlay volume exported through collection, in block units |
| `geology.inventory_MATERIAL`, `geology.remainder_MATERIAL` | number | Whole blocks and remainder units (0–1023); MATERIAL is `ash`, `basalt`, `obsidian`, `scoria`, `pumice`, or `rhyolite` |
| `geology.cell_ash_height` | number | Ash volume / 1024 at the player cell, zero when absent |
| `geology.cell_silica`, `geology.cell_vesicles`, `geology.cell_quenched` | number | Overlay material marker fractions in [0,1] |
| `geology.cell_rock` | string | Predicted overlay product, final solid block name (`basalt`, `rhyolite`, `obsidian`, `scoria`, `pumice`), or `none` |

`deposit` supplies explicit debug material through the ordinary bounded deposit path.
X/Y/Z are integer local world coordinates, UNITS is 1–1024 (1024 = one block).
It reports requested/accepted units, can partially fill a cell, and aborts the
DSL command if nothing is accepted. Debug supply is counted in runtime ledgers but is not debited from a
vent. Lava requires a loaded cell inside an admitted vent domain; create and stop
a forced vent first when testing recipes. Ash only requires the loaded observer
neighborhood. Solid blocks, water and the other overlay reject deposition.

`geology inventory MATERIAL BLOCKS` sets the whole-block inventory count for one
of the six collection materials (0–999), preserving its remainder. This is explicit
debug setup, useful for full-inventory tests, and does not change source ledgers.
Typed inventory fields are read-only and never move a deposit or grant items.

Ash fallout debits the vent reservoir after lava emission: at intensity 1 it requests
32 units per 0.5-second erupting tick (8 while waning). Deterministic targets span a
7×7 grid with weather wind drift. Fallout checks the 12 cells above the outlet,
then searches from outlet Y+12 down to Y−24 for known support. A roof or unknown
column blocks emission/deposition. Deposits stack and fall one voxel per 0.1-second
step when support disappears; height is volume/1024 for rendering, collision and rays.
At most 2048 ash records are retained. Beyond ±96 blocks from the observer, unloaded
cells and other bodies park. Full ash remains erodible overlay, not a base block.
Rain/drizzle exports up to 2 units per second from exposed top layers. Exposure uses
a bounded 12-cell canopy probe; roofs farther above are outside this approximation.
Washed sediment leaves this local model; mudflow, water transport of sediment and
fractional placement are not implemented. Ordinary edits cannot erase ash layers;
left-click collection uses the volume transaction described below.

Lava source composition is a deterministic archetype from the vent identity/seed;
forced legacy vents supply dense mafic material. The `silicic` debug recipe supplies
dense silica-rich melt, while `scoria` and `pumice` supply gas-rich mafic/silicic melt.
Markers mix and move proportionally with volume. Water-committed freezing adds a
bounded quench-history marker; a failed water commit adds nothing. This is a
homogeneous, fixed-density gameplay approximation: quench history survives reheating
and transport, with no bubble expansion, gas pressure or glass recrystallization.

| Composition / cooling | Product |
| --- | --- |
| Mafic, vesicle marker < 0.5 | Basalt |
| Silicic (silica marker ≥ 0.5), vesicles < 0.5, quenched < 0.25 | Rhyolite |
| Silicic, vesicles < 0.5, quenched ≥ 0.25 | Obsidian |
| Mafic, vesicles ≥ 0.5 | Scoria |
| Silicic, vesicles ≥ 0.5 | Pumice |

A full cell commits its selected rock when it reaches ambient temperature; partial
solid cells retain height and material. Product thresholds are gameplay rules,
not a mineral crystallization solver. Without debug supply, each vent obeys
`reservoir + cumulative lava supply + cumulative ash supply = 256` blocks.

Use `make test-volcano-e2e` for a real Wayland route that covers force, pause,
manual stepping, mid-eruption save/load, parking, resume, stop and a second
event, followed by a closed-basin cooling/solidification check and a separate
two-vent route that verifies parked lava does not block a loaded vent's recovery.
The water-contact route covers finite and enclosed pools, mid-reaction save/load,
unloading/return, steam output and final basalt. Another route checks `target.hit`
and `target.position` above and inside a cooled
partial deposit, including looking down at its actual top surface.
The ash route checks fallout inventory, thin-layer selection, rain, roof cover,
support removal, unload parking and save/load continuation. The material route
checks all five full-block products and water-quench save/load. Run an individual
route with `bash tests/test_volcano_e2e.sh build/normal/voxelcraft volcano_ash_layers`.
It retains the
isolated runtime directories, replies, traces and screenshots. Every geology
tick executes five lava and ash steps; its weather sample time stays frozen during the
command. Water flow does not advance with `geology step`; use explicit `fluid step`
when testing transport. Unloaded contacts park without offline catch-up.
After `load`, use `stream wait` before inspecting water or stepping reactions.
Water and ordinary block writes cannot overwrite an ash/lava/partial-solid
overlay. Left-click collection removes an ash layer or an ambient-cooled rock
cell by its actual volume.
Full cooled rock becomes a normal block. Storage and domain limits are described
in [the geology implementation record](architecture/18-volcano-preview.md).

Left-click collection keeps separate remainders for all six materials: 1024
units become one usable inventory block. For example, collecting a 512-unit ash
layer gives zero blocks plus 50%; collecting another 512 gives one block and zero
remainder. One click removes the entire targeted overlay cell, up to 1024 units.
The selected hotbar label and successful collection notice show fractional progress.
Hot or partly liquid lava cannot be collected, even if its crust can be targeted.
At 999 whole blocks, collection fails without changing the source or remainder;
a remainder already held at that limit survives until inventory space opens.
Different materials never combine. Normal full base blocks keep the ordinary
collection/placement behavior, and inventory placement still requires a whole block.

Source removal, heat/volume export and inventory credit commit together on the
main thread. Removal rechecks the source volume and rock type. Collected ash
satisfies `supplied = volume + washed + collected`; lava collection uses its
existing `removed` and `exportedEnergy` counters. Full rock already exported to
base terrain remains in `solidified`, not counted twice as overlay collection.
Overlay removal adds no base-block undo record, immediately updates collision/rays,
and wakes adjacent water; unsupported ash above falls on the next geology step.
No terrain mesh rebuild is needed. For overlay collection the mouse lifecycle's
`set_block_frame` and `mesh_frame` both report the successful collection frame,
and `mesh_ready` is immediately true. These fields are zero on failed clicks.

`volcano_collection.dsl` verifies real left clicks, save/load between two half-block
collections, full-inventory refusal, falling ash after support mining, rain-reduced
collection and separate basalt/pumice remainders. `volcano_rejected_deposit.dsl`
expects exit code 3 and confirms that failed geology setup aborts the script.

Water contact checks six faces after lava flow. Side faces use their overlapping
height; vertical neighbors require the lower cell to be full. Each successful
1/255-block water parcel consumes 4096 normalized enthalpy units for heating
from 293 K and vaporization. Fractional face credits are saved but carry no heat:
energy stays in lava until water commits. There is no transported water temperature,
steam pressure or condensation. `energy_error` includes `water_energy` in its balance.
Steam follows committed vaporization, with at most 24 cells × four puffs; the
visual strength is transient and zero immediately after load. Inspect prints
water totals and the player's lava cell temperature, credit and steam strength.

For example, after starting a flat world and settling its stream:

```text
pause on
geology force eruption 1 120
let vent = geology.id
geology step 60
assert geology.phase == "erupting"
save
geology step 10
load
assert geology.id == vent && geology.ticks == 60
geology stop ${vent}
```

### Ships

```text
ship begin
ship enter
ship input FORWARD STRAFE VERTICAL FRAMES
ship exhaust DEMAND
ship dust
```

Ship input components are in `[-1,1]` for 1-600 frames; exhaust demand is
0-1. `ship begin` starts a debug flight, while `ship enter` enters a local
recorded ship. Successful commands reply with `ship begin ok position=...`,
`ship enter ok block=...`, `ship input ok ... frames=...`,
`ship exhaust ok demand=...`, or `ship dust ok`. Invalid state replies identify
`not_playing`, `not_driving`, `no_recorded_ship`, `ship_not_local`,
`ship_unavailable`, `ship_exhaust_invalid`, or `no_surface_ship` as
appropriate. Input, exhaust, and dust require the appropriate driving/surface
state. The typed fields `ship.driving` and `ship.mode` are available for DSL
assertions; the current exhaust demand and scripted input frame count are
available in the `status` reply as `ship_exhaust` and `ship_input_frames`.

### Map markers

```text
marker add X Z COLOR NAME
marker list
marker target ID|none
marker remove ID
```

Colors are `red`, `amber`, `green`, `cyan`, `blue`, and `magenta`. Coordinates
are bounded to +/-1,000,000 and marker names are limited to 63 bytes. Marker
commands require an active surface world.

### Evolution

```text
evolution inspect [RADIUS]
evolution focus [RADIUS]
evolution region
evolution bootstrap status
evolution advance DAYS
evolution atlas
evolution preview ground|flight|aquatic SEED LENGTH WIDTH APPENDAGES SILHOUETTE
evolution pose ACTION SPEED PHASE TURN
evolution pose live
evolution pose freeze
evolution scenario ENERGY HYDRATION FATIGUE STRESS FOOD WATER SHELTER STORM DANGER
evolution catalog
```

`evolution preview` opens the specimen lab with a fixed view and decimal seed.
The three proportions are integer percentages (50–180); `SILHOUETTE` is 0 or 1.
It changes only the lab specimen. For example, `evolution preview ground 123 100
100 100 0` followed by `screenshot` captures the crawler. Use `flight` and
`aquatic` with the same seed to compare plans, or a final `1` for a solid
silhouette. In the lab, Space toggles rotation and V toggles silhouette mode.

After opening `evolution preview`, `evolution pose` selects a reproducible,
frozen animation snapshot. `ACTION` is `idle`, `wander`, `seek_habitat`,
`forage`, `seek_food`, `drink`, `seek_water`, `rest`, `seek_shelter`, `flee`,
`hunt`, `scavenge`, `mate`, `nest`, `alert`, or `dead`. `SPEED` is 0–8 metres
per second, `PHASE` is a cycle in [0,1), and `TURN` is a normalized turn rate
in [-1,1]. These values affect only the lab specimen. The preview deliberately
allows combinations such as a stationary attack, without changing live animal
needs or AI. `hunt`, `scavenge`, and `mate` preview contact with a target.

```text
evolution preview ground 123 100 100 100 0
evolution pose forage 0 0.48 0
assert evolution.preview_action == "forage" && evolution.preview_frozen
screenshot
evolution pose flee 2 0.35 0.7
screenshot
evolution pose live
assert !evolution.preview_frozen
```

`evolution pose live` resumes the chosen action, speed and turn with smooth
phase progression. Reopening `evolution preview` resets it to idle. Typed
fields `evolution.preview_action` (string), `preview_speed`, `preview_phase`,
`preview_turn` (numbers) and `preview_frozen` (bool), all under `evolution.`,
describe the lab configuration. Run `make test-animal-pose-e2e` on Hyprland to
assert the command path and capture 39 ground/flight/aquatic poses, including
opposite walking phases, rest, feeding, vigilance, courtship, strikes and
death. The harness always retains its temporary screenshots and reply log.

`evolution pose freeze` pauses at the current pose without resetting its phase.
`evolution scenario` requires an open specimen preview. Its first eight values
are finite numbers in [0,1]; `DANGER` is an integer: 0 safe, 1 alert, 2 threatened.
It calls the same `FaunaBehaviorEvaluate` used by world animals, then freezes
the selected pose at phase 0.48. Use `evolution pose live` to play its animation.
Replies contain the selected action, utility score and all supplied conditions.
`evolution.scenario_valid`, `evolution.scenario_action` and
`evolution.scenario_utility` expose the decision for assertions. Reopening the
preview or selecting a different action clears the decision; freezing, resuming
or adjusting the same action's phase preserves it.

This is a single decision under fixed conditions, not a live-world simulation.
The fixture assumes a reachable better food/water/shelter location when its
local availability is below 0.2, uses full food/water dependence and activity,
starts from idle and disables random wandering. Danger is supplied directly;
the fixture does not test player-distance perception, pregnancy, target selection,
pathfinding or contact damage. Needs stay fixed during animation playback.

```text
evolution preview ground 123 100 100 100 0
# Hungry, with food locally available.
evolution scenario 0.1 1 0 0 1 1 1 0 0
assert evolution.scenario_valid && evolution.scenario_action == "forage"
# The same needs, now threatened: safety interrupts feeding.
evolution scenario 0.1 1 0 0 1 1 1 0 2
assert evolution.scenario_action == "flee"
evolution pose live
evolution pose freeze
screenshot
```

#### Interactive animal scenarios

From the project directory in Hyprland, run `make animal-scenarios`. The terminal
menu selects 12 fixed situations: calm, hunger with/without local food, thirst
with/without local water, fatigue, exposed/sheltered storm, alert while hungry,
danger while hungry, hunger/thirst competition, and danger while exhausted.
Each preset asserts the game's actual decision before playing it. Presets do
not inject the expected action. This lab isolates the base need/alert decision;
world target selection and evolution-specific overrides remain outside its scope.

| Menu input | Effect |
| --- | --- |
| `1`–`12` | Select conditions, assert the expected decision, play the result |
| `g`, `f`, `w` | Repeat the current conditions with ground, flight or aquatic anatomy |
| `p` | Pause/resume at the current pose |
| `[`, `]` | Set the phase 0.1 cycles backward/forward and freeze |
| `v`, `t` | Set display speed or turn; these do not change the decision inputs |
| `c` | Enter all nine scenario values, and inspect the computed action/score |
| `o` | Pose-only preview, including hunt/scavenge/mate/nest/dead; no trigger assertions |
| `r` | Reopen the fixed-seed specimen and repeat current conditions/pose |
| `s` | Save a PNG and its capture report |
| `a` | Assert/capture all presets for the current anatomy and check threat recovery |
| `i`, `h`, `q` | Show current configuration, show help, quit |

All runs use a separate `/tmp/voxelcraft-animal-scenarios-*` working directory
with an assets symlink. The directory is printed and retained on success or
failure. `replies.log` holds the complete game output; `replay.dsl` holds the
submitted commands. To replay a session in the same isolated directory:

```sh
cd /tmp/voxelcraft-animal-scenarios-REPLACE_ME
/absolute/path/to/cube/build/normal/voxelcraft --debug-script replay.dsl --debug-stdin
```

An interactive replay ends with its last submitted command and leaves the game
open. It reproduces inputs and frozen phases; time spent watching live playback
between menu commands is not recorded. The game output is continuously drained
while the terminal waits for input. EOF, Ctrl-C and `q` close the child process.

`make test-animal-scenarios-e2e` checks all 12 presets for all three anatomies,
threat recovery, playback advancement and exact pause preservation. It retains
36 fixed screenshots plus one pause screenshot. This checks decisions and the
rendering command path; screenshots still need visual review. The standalone
`make test-animal-scenarios-script` checks the subprocess protocol without a GPU.
Use `uv run --no-project --offline python scripts/animal_scenarios.py --help`
for options, including `--seed`, `--plan`, `--binary`, `--resolution`, `--list`
and `--check`.

`evolution advance` advances the simulation clock and begins bounded regional
catch-up. For large jumps, use `wait until !ecology.catchup_pending timeout 600`
before checking updated populations. Region inspection and the typed ecology
fields are read-only; they no longer advance the simulation implicitly.

Inspect/focus radii default to 24 and range from 1 to 256. `advance` accepts
0.25-4096 days. `region` and `evolution bootstrap status` report the current
ecology region; `atlas` toggles the biology atlas and `catalog` reports catalog
counts. The typed `evolution.*` fields above expose the same region snapshot for
assertions: `evolution.region_available` is false before play or outside a
surface, and the remaining fields return deterministic zero/false defaults
while it is unavailable.

## Reply protocol

Replies are newline-delimited text on the debug output stream:

* `DEBUG_SCRIPT loaded path=... batch=0|1` confirms a file was parsed.
* `DEBUG_SCRIPT complete source=stdin|PATH` confirms a non-exit block finished.
* `DEBUG_SCRIPT exit code=N` precedes process termination through `exit`.
* `DEBUG_SCRIPT error source=... line=L column=C code=NAME message=...` reports
  parse, type, assertion, timeout, or callback failures.
* `DEBUG_CONTROL ...` carries readiness and command-specific diagnostics.

The human-readable command reply is useful for logs, but assertions should use
typed runtime fields rather than parsing `status` text. A command failure is
reported both in its `DEBUG_CONTROL` line (when the command produces one) and
as a `DEBUG_SCRIPT error` callback failure. The same diagnostic is also written
to stderr.

## Complete example

The following script starts from the menu, waits for the world and local stream
to settle, checks streaming state, enables water geometry bounds, captures a
screenshot, and exits successfully. Save it as `/tmp/water-check.dsl`:

```sh
cat > /tmp/water-check.dsl <<'DSL'
# This file intentionally ends with exit, so it is a batch run.
let origin = [15, 110, -252]
assert game.screen == "start"
assert settings.autosave == false
start
wait until game.screen == "playing" timeout 7200
teleport ${origin.x} ${origin.y} ${origin.z} 3.141593 -0.25
stream wait 300
wait until stream.surface_ready timeout 600
assert stream.audit_complete
water debug on
assert render.water_debug
status
screenshot
repeat 2 {
  status
}
exit 0
DSL
build/normal/voxelcraft --debug-script /tmp/water-check.dsl
```

For interactive control, start with `--debug-script PATH --debug-stdin` and
omit the file's final `exit`. Send blocks after the `DEBUG_SCRIPT complete`
reply:

```text
assert game.screen == "playing"
assert water.eyes_submerged
exit 0
```

## Troubleshooting

* No window or no `DEBUG_CONTROL ready`: verify an active Hyprland session,
  `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`, and the socket path. This implementation
  does not fall back to X11/Xvfb.
* `DEBUG_SCRIPT loaded ... batch=1` but stdin is silent: the script ends in
  `exit`, so stdin handoff is intentionally disabled.
* `code=undefined`: check the exact runtime name or define a `let` first.
* `code=type`: check boolean requirements for `assert`/`wait`, numeric timeout
  and repeat values, and vector field access.
* `code=assertion`: inspect the field with `status` or a smaller assertion; the
  process only exits for this error when the failing file is a batch.
* `stream wait result=timeout`: increase the frame budget (maximum 3,600),
  confirm the player is in a surface world, and inspect the reported pending
  section count. A timeout aborts the current block.
* `command rejected: not_playing` or `not_in_surface_world`: issue `start` and
  wait for `game.screen == "playing"` before the command; surface-only commands
  cannot run in space or from the menu.
* `use the DSL exit statement instead of quit`: replace `quit` with `exit N`.
* `code=syntax` at EOF or a brace: send the complete `repeat` block, including
  its closing `}`, in one stdin sequence. Keep `exit` last at top level.
