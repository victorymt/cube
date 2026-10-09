terrain varied
start
pause on
surface debug planet desert 1235
geology enabled off
weather force clear 1 36000
geology find history face 0
assert geology.terrain_found
geology history world
assert geology.history_world_ready && geology.history_world_depth > 0
let center = geology.terrain_position
teleport ${center.x-50} ${center.y+75} ${center.z+50} 2.4 -0.8
stream wait 1200
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 1200
pause on
teleport ${center.x-50} ${center.y+75} ${center.z+50} 2.4 -0.8
stream wait 1200
pause off
screenshot
exit 0
