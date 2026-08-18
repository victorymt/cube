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
```

`--debug-script` accepts exactly one readable file. A missing path, an empty
path, a path that is too long, or a repeated option is a startup error. The
file is loaded before the first frame. `--debug-stdin` reads complete DSL
blocks from standard input. The options can be combined; the file is loaded
first and stdin is then available.

Every enabled run disables autosave and uses the fixed 60 FPS debug clock. On
startup the process writes a readiness line similar to:

```text
DEBUG_CONTROL ready mode=dsl commands=start,screenshot,status,stream,save,load,map,marker,teleport,look,input,ship,view,fluid,water,evolution statements=let,assert,wait,repeat,exit
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
| `player.position` | vec3 | Player world position |
| `player.velocity` | vec3 | Player velocity |
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
| `target.hit` | bool | Current solid raycast hit exists |
| `target.position` | vec3 | Current raycast block position; only meaningful when `target.hit` is true |
| `target.block` | string | Target block name, or `air` when there is no hit |
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
screenshot
save
load
map
water debug
water debug on|off
water debug through
water debug through on|off
view first|third
```

`water debug` and `water debug through` toggle their current setting when the
`on`/`off` argument is omitted. `start` is valid on the start screen.
`screenshot` first reports
`DEBUG_CONTROL screenshot scheduled`; after the frame capture it reports
`DEBUG_CONTROL capture ok png=PATH report=PATH`. The report is a key/value text
file next to the PNG. `map` toggles the surface map and requires an active
surface world. `save` and `load` require the playing screen.

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
DEBUG_CONTROL stream wait result=settled elapsed_frames=... focus=... pending_local_sections=0 pending_gen=... pending_mesh=... pending_mesh_snapshot_bytes=... missing_surface_chunks=...
```

If the local region is not settled by the timeout, the result is
`result=timeout`, the current DSL executor is aborted with a timeout error, and
the rest of that block is not executed. The wait can only run while playing in
a surface world and cannot overlap an audit. `stream audit` scans a radius
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
