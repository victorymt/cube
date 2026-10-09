terrain varied
start
pause on
load
assert !geology.enabled
geology find history
assert geology.terrain_found
geology history world
assert geology.history_world_ready && geology.history_world_depth == 13
assert geology.history_represented == 42976
let center = geology.terrain_position
teleport ${center.x+0.1} ${center.y-0.99} ${center.z+0.1} 0 0
stream wait 1200
assert geology.cell_collision_height == 1 && geology.cell_rock == "basalt"
assert geology.cell_volume == 0
exit 0
