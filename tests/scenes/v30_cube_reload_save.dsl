# V30 fresh-process recovery, phase 1: create a cube Homeworld edit.
assert game.screen == "start"
start
wait until game.screen == "playing" timeout 7200
surface debug cube
wait until world.surface_topology == "cube-grid" timeout 1800
wait until stream.cube.active timeout 1800
stream wait 1200
block set 169 84 297 Glass
save
exit 0
