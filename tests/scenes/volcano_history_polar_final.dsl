terrain varied
terrain history on
start
pause on
surface debug planet desert 1235
geology enabled off
weather force clear 1 36000
geology find history polar
assert geology.terrain_found
assert geology.terrain_abs_latitude > 71
geology history world
assert geology.history_world_ready && geology.history_world_depth > 0
let center = geology.terrain_position
teleport ${center.x-50} 220 ${center.z+50} 2.4 -0.8
stream wait 1200
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 1200
pause on
teleport ${center.x-50} 220 ${center.z+50} 2.4 -0.8
stream wait 1200
pause off
screenshot
exit 0
