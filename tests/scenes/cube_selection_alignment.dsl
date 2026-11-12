# Face-edge selection probe: the white target and green placement wireframes
# must use the same projected cube-cell corners as the visible terrain.
assert game.screen == "start"
terrain flat
start
wait until game.screen == "playing" timeout 7200
surface debug cube
wait until world.surface_topology == "cube-grid" timeout 1800
wait until stream.cube.active timeout 1800
wait until stream.surface_ready timeout 1800
surface address 0 4095 2048 1.0 0 -0.8
wait until stream.surface_ready timeout 1800
input 0 0 0 0 30
render pick 640 360
assert render.pick.hit
assert target.hit
screenshot
exit 0
