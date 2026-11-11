# Deterministic visual probe for the world-surface render debug modes.
assert game.screen == "start" && !settings.autosave
terrain flat
start
wait until game.screen == "playing" timeout 7200
wait until stream.surface_ready timeout 1800
weather force clear 0.0 36000
weather damage off
world time 0.375
view distance 1
block set -1 9 8 Stone
block set 0 9 8 Stone
block set 1 9 8 Stone
block set -1 10 8 Stone
block set 0 10 8 Stone
block set 1 10 8 Stone
block set -1 11 8 Stone
block set 0 11 8 Stone
block set 1 11 8 Stone
block set -1 12 8 Stone
block set 0 12 8 Stone
block set 1 12 8 Stone
pause on
teleport 0 9 0 0 -0.12
pause off
look 0 -0.12
stream wait 600
view first
render debug final
let final_render_frame = debug.frame
wait until debug.frame > final_render_frame + 2 timeout 120
assert render.debug_mode == "final"
screenshot
let final_capture_frame = debug.frame
wait until debug.frame > final_capture_frame + 2 timeout 120
render debug albedo
let albedo_render_frame = debug.frame
wait until debug.frame > albedo_render_frame + 2 timeout 120
assert render.debug_mode == "albedo"
screenshot
let albedo_capture_frame = debug.frame
wait until debug.frame > albedo_capture_frame + 2 timeout 120
render debug normal
let normal_render_frame = debug.frame
wait until debug.frame > normal_render_frame + 2 timeout 120
assert render.debug_mode == "normal"
screenshot
let normal_capture_frame = debug.frame
wait until debug.frame > normal_capture_frame + 2 timeout 120
render debug ao
let ao_render_frame = debug.frame
wait until debug.frame > ao_render_frame + 2 timeout 120
assert render.debug_mode == "ao"
screenshot
let ao_capture_frame = debug.frame
wait until debug.frame > ao_capture_frame + 2 timeout 120
render debug diffuse
let diffuse_render_frame = debug.frame
wait until debug.frame > diffuse_render_frame + 2 timeout 120
assert render.debug_mode == "diffuse"
screenshot
let diffuse_capture_frame = debug.frame
wait until debug.frame > diffuse_capture_frame + 2 timeout 120
render debug specular
let specular_render_frame = debug.frame
wait until debug.frame > specular_render_frame + 2 timeout 120
assert render.debug_mode == "specular"
screenshot
let specular_capture_frame = debug.frame
wait until debug.frame > specular_capture_frame + 2 timeout 120
render debug shadow
let shadow_render_frame = debug.frame
wait until debug.frame > shadow_render_frame + 2 timeout 120
assert render.debug_mode == "shadow"
screenshot
let shadow_capture_frame = debug.frame
wait until debug.frame > shadow_capture_frame + 2 timeout 120
render debug off
assert render.debug_mode == "final"
exit 0
