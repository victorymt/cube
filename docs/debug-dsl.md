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
```

## Starting a debug session

There are two entry points:

```sh
build/normal/voxelcraft --debug-stdin
build/normal/voxelcraft --debug-script PATH
build/normal/voxelcraft --debug-script=PATH
build/normal/voxelcraft --debug-resolution 1920x1080 --debug-script PATH
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

Every enabled run disables autosave and uses the fixed 60 FPS debug clock. On
startup the process writes a readiness line similar to:

```text
DEBUG_CONTROL ready mode=dsl commands=start,screenshot,status,world,stream,save,load,map,surface,marker,teleport,look,input,ship,view,fluid,water,weather,evolution,block,flora statements=let,assert,wait,repeat,exit
```

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
| `player.position` | vec3 | Player world position |
| `player.velocity` | vec3 | Player velocity |
| `player.input_frames` | number | Remaining scripted player-input frames |
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
| `fluid.volume` | number | Fluid volume at the player |
| `fluid.surface_y` | number | Fluid surface at the player |
| `fluid.queue_overflows` | number | Fluid solver queue overflow count |
| `weather.climate` | string | Local named climate regime, or `"unavailable"` outside a surface world |
| `weather.phenomenon` | string | Dominant current weather phenomenon |
| `weather.cloud_genus` | string | Dominant WMO cloud genus, or `None` |
| `weather.cloud_cover` | number | Aggregate cloud cover from 0 to 1 |
| `weather.cloud_flags` | number | Bit mask of all WMO cloud genera currently present |
| `weather.cloud_layers` | number | Number of bounded renderer cloud layers (0-3) |
| `weather.cloud_forced_frames` | number | Remaining forced-cloud update frames |
| `weather.temperature`, `weather.temperature_k` | number | Local air temperature in kelvin |
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

### Session, rendering, and world

```text
start
status
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
```

Movement components are in `[-1,1]`; `SPRINT` is `0` or `1`; scripted input
lasts 1-600 frames. Teleport coordinates are finite and bounded to +/-1,000,000,
yaw to +/-1,000, and pitch to [-1.45, 1.45]. Absolute look uses the same pitch
range; relative look deltas are bounded to +/-1,000 and the resulting pitch is
clamped.

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

Forced weather is runtime-only and is never saved. `weather clear` returns to
the natural field and clears separately forced cloud and tornado state.
`weather cloud` forces
one WMO cloud genus without replacing a forced weather phenomenon; `COVERAGE`
is 0-1, `FRAMES` is 1-36,000, and `weather cloud clear` restores natural cloud
classification. The accepted genera are `cirrus`, `cirrocumulus`,
`cirrostratus`, `altocumulus`, `altostratus`, `nimbostratus`,
`stratocumulus`, `stratus`, `cumulus`, and `cumulonimbus`. Cloud forcing is
also runtime-only. `weather inspect` reports the dominant and present genera,
the selected renderer layers, and each layer's coverage, base, and thickness.

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
time. Inspect, force, and step require an active surface world.

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
recorded ship. Input, exhaust, and dust require the appropriate driving/surface
state.

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
evolution catalog
```

Inspect/focus radii default to 24 and range from 1 to 256. `advance` accepts
0.25-4096 days. `region` and `evolution bootstrap status` report the current
ecology region; `atlas` toggles the biology atlas and `catalog` reports catalog
counts.

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
