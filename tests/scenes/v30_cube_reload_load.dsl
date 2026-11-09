# V30 fresh-process recovery, phase 2: load without the debug cube command.
assert game.screen == "start"
start
wait until game.screen == "playing" timeout 7200
load
wait until world.surface_topology == "cube-grid" timeout 1800
wait until stream.cube.active timeout 1800
stream wait 1200
block set 169 84 297 Dirt
assert world.surface_topology == "cube-grid"
exit 0
