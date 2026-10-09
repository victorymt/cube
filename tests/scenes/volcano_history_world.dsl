terrain varied
start
pause on
surface debug planet desert 1234
geology enabled off
view distance 12
weather force clear 1 36000
geology find history
assert geology.terrain_found
geology history world
assert geology.history_world_ready && geology.history_world_depth > 0
assert geology.history_represented > 1000 && geology.history_quantization_loss >= 0
assert geology.history_rejected == 0
let center = geology.terrain_position
let represented = geology.history_represented
let depth = geology.history_world_depth
teleport ${center.x+0.1} ${center.y-0.99} ${center.z+0.1} 0 0
stream wait 1200
assert geology.cell_collision_height == 1 && geology.cell_rock != "none"
assert geology.cell_volume == 0
let material = geology.cell_rock
save
load
stream wait 1200
geology history world
assert geology.history_represented == represented && geology.history_world_depth == depth
assert geology.cell_rock == material && geology.cell_collision_height == 1
assert !geology.enabled
teleport ${center.x-50} ${center.y+100} ${center.z+55} 2.4 -1.0
stream wait 1200
pause off
input 0 0 0 0 120
wait until player.input_frames == 0 timeout 1200
pause on
teleport ${center.x-50} ${center.y+100} ${center.z+55} 2.4 -1.0
stream wait 1200
pause off
screenshot
exit 0
