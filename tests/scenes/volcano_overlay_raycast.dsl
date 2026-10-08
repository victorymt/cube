terrain flat
start
teleport 0 14 0 1.570796 -0.15
stream wait 600
pause on
geology force eruption 1 120
let vent = geology.id
geology step 120
geology stop ${vent}
geology step 24000
teleport 24.5 9.01 0.5 1.570796 0
assert geology.cell_volume > 0 && geology.cell_volume < 0.75
assert geology.cell_collision_height == geology.cell_volume
let thickness = geology.cell_volume
# Eyes are 1.55 above the player origin. Start in the empty part of this voxel.
teleport 24.5 8.2 0.5 1.570796 0
assert !target.hit
# The same voxel remains selectable when looking at its actual upper surface.
look 1.570796 -1.4
assert target.hit && target.position.x == 24 && target.position.y == 9
assert target.block == "Basalt"
# A horizontal ray within the thin layer hits the deposit too.
teleport 24.5 ${9+thickness/2-1.55} 0.5 1.570796 0
assert target.hit && target.position.x == 24 && target.position.y == 9
exit 0
