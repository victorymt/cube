terrain varied
terrain history on
start
pause on
surface debug planet desert 1234
geology enabled off
view distance 12
weather force clear 1 36000
geology find history
assert geology.terrain_found
assert geology.terrain_position.y == 147
geology history world
assert geology.history_world_ready && geology.history_world_depth == 13
teleport -455 220 -345 2.4 -0.8
stream wait 1200
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 1200
pause on
teleport -455 220 -345 2.4 -0.8
stream wait 1200
pause off
screenshot
exit 0
