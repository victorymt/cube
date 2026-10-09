terrain varied
start
pause on
surface debug planet desert 1234
geology enabled off
geology find history
geology history world
assert geology.history_world_ready && geology.history_world_depth > 1
let center = geology.terrain_position
let represented = geology.history_represented
teleport ${center.x+0.1} ${center.y-0.99} ${center.z+0.1} 0 0
stream wait 1200
assert geology.cell_collision_height == 1 && geology.cell_rock != "none"
block set ${center.x} ${center.y-1} ${center.z} air
assert geology.cell_collision_height == 0 && geology.cell_volume == 0
save
load
stream wait 1200
assert geology.cell_collision_height == 0 && geology.cell_volume == 0
geology history world
assert geology.history_represented == represented
block set ${center.x} ${center.y-1} ${center.z} basalt
assert geology.cell_collision_height == 1 && geology.cell_rock == "basalt"
save
load
stream wait 1200
assert geology.cell_collision_height == 1 && geology.cell_rock == "basalt"
assert geology.cell_volume == 0
exit 0
