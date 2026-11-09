# M4.2: cube fluid must carry a finite water volume across a face edge.
assert game.screen == "start"
terrain flat
start
wait until game.screen == "playing" timeout 7200
pause on
teleport -1209 9 1903 0 0
pause off
stream wait 1200
surface debug cube
wait until world.surface_topology == "cube-grid" timeout 1800
wait until stream.cube.active timeout 1800
view distance 1
surface pose -0.9272913863 0.7297913598 9 0 0
input 0 0 0 0 120
wait until player.input_frames == 0 timeout 300
fluid set -2418 9 1903 255
fluid step 30
fluid inspect -2417 9 1903
assert fluid.inspect_volume > 0
assert fluid.loaded_volume == 255
exit 0
