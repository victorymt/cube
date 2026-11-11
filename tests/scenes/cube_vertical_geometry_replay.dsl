# A flat 5x5 stone wall gives both backends identical vertical geometry.
# Capture it once from legacy and once from cube after the same block edits.
assert game.screen == "start"
terrain flat
start
wait until game.screen == "playing" timeout 7200
surface debug home
wait until stream.surface_ready timeout 1800
world time 0.5
weather force clear 0.0 36000
weather damage off
block set -2 9 8 Stone
block set -1 9 8 Stone
block set 0 9 8 Stone
block set 1 9 8 Stone
block set 2 9 8 Stone
block set -2 10 8 Stone
block set -1 10 8 Stone
block set 0 10 8 Stone
block set 1 10 8 Stone
block set 2 10 8 Stone
block set -2 11 8 Stone
block set -1 11 8 Stone
block set 0 11 8 Stone
block set 1 11 8 Stone
block set 2 11 8 Stone
block set -2 12 8 Stone
block set -1 12 8 Stone
block set 0 12 8 Stone
block set 1 12 8 Stone
block set 2 12 8 Stone
block set -2 13 8 Stone
block set -1 13 8 Stone
block set 0 13 8 Stone
block set 1 13 8 Stone
block set 2 13 8 Stone
pause on
teleport 0 9 0 0 -0.05
pause off
input 0 0 0 0 30
wait until !game.paused timeout 120
screenshot
surface debug cube
wait until world.surface_topology == "cube-grid" timeout 1800
wait until stream.cube.active timeout 1800
wait until stream.surface_ready timeout 1800
world time 0.5
weather force clear 0.0 36000
weather damage off
pause on
teleport 0 9 0 0 -0.05
pause off
wait until stream.surface_ready timeout 1800
wait until player.velocity.y == 0 timeout 600
block set -2 9 8 Stone
block set -1 9 8 Stone
block set 0 9 8 Stone
block set 1 9 8 Stone
block set 2 9 8 Stone
block set -2 10 8 Stone
block set -1 10 8 Stone
block set 0 10 8 Stone
block set 1 10 8 Stone
block set 2 10 8 Stone
block set -2 11 8 Stone
block set -1 11 8 Stone
block set 0 11 8 Stone
block set 1 11 8 Stone
block set 2 11 8 Stone
block set -2 12 8 Stone
block set -1 12 8 Stone
block set 0 12 8 Stone
block set 1 12 8 Stone
block set 2 12 8 Stone
block set -2 13 8 Stone
block set -1 13 8 Stone
block set 0 13 8 Stone
block set 1 13 8 Stone
block set 2 13 8 Stone
input 0 0 0 0 30
wait until !game.paused timeout 120
assert world.surface_topology == "cube-grid"
screenshot
pause on
look 0 -0.8
pause off
wait until !game.paused timeout 120
render pick 640 360
assert render.pick.hit
assert render.pick.address.face >= 0
let break_face = render.pick.address.face
let break_u = render.pick.address.u
let break_v = render.pick.address.v
let break_radial = render.pick.address.radial
mouse left
wait until interaction.mouse_left_set_block_frame > 0 timeout 120
wait until interaction.mouse_left_mesh_ready timeout 600
render pick 640 360
assert !render.pick.hit || render.pick.address.face != break_face || render.pick.address.u != break_u || render.pick.address.v != break_v || render.pick.address.radial != break_radial
exit 0
