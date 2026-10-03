terrain flat
start
teleport 0 14 0 1.570796 -0.15
stream wait 600
pause on
assert geology.records == 0
geology inspect
assert geology.records == 0
geology force eruption 1 120
assert geology.present && geology.active && geology.phase == "unrest"
assert geology.event == 1 && geology.records == 1
let vent_id = geology.id
let vent_position = geology.position
let reservoir = geology.reservoir
geology step 60
assert geology.phase == "erupting"
assert geology.ticks == 60 && geology.reservoir == reservoir
save
let saved_ticks = geology.ticks
geology step 10
assert geology.ticks == saved_ticks + 10
assert geology.supplied > 0 && geology.volume_error == 0
load
assert geology.id == vent_id && geology.ticks == saved_ticks
assert geology.phase == "erupting" && geology.event == 1
geology step 60
assert geology.lava_volume > 0 && geology.reservoir < reservoir
assert geology.volume_error == 0
let hot_volume = geology.lava_volume
let hot_supply = geology.supplied
save
geology step 10
load
assert geology.lava_volume == hot_volume && geology.supplied == hot_supply
geology inspect
teleport 0 14 0 1.570796 -0.25
pause off
screenshot
pause on
let ticks_before_parking = geology.ticks
teleport 1000 14 0 1.570796 -0.25
geology step 20
assert !geology.active && geology.ticks == ticks_before_parking
assert geology.id == vent_id && geology.event == 1
teleport 0 14 0 1.570796 -0.25
stream wait 600
geology step 1
assert geology.active && geology.ticks == ticks_before_parking + 1
geology stop ${vent_id}
assert geology.phase == "cooling"
assert geology.volume_error == 0
geology step 1500
assert geology.phase == "recovery"
assert geology.hot_cells == 0 && geology.liquid_volume == 0
assert geology.solid_volume > 0 || geology.solidified > 0
assert geology.volume_error == 0
teleport ${vent_position.x} ${vent_position.y+0.01} ${vent_position.z} 1.570796 -0.25
assert geology.cell_volume > 0 && geology.cell_volume < 1
assert geology.cell_collision_height == geology.cell_volume
assert geology.cell_liquid == 0
teleport 0 14 0 1.570796 -0.25
pause off
screenshot
pause on
geology force eruption 0.5 10
assert geology.event == 2 && geology.id == vent_id
geology vents
geology stop ${vent_id}
teleport 32 14 0 1.570796 -0.25
stream wait 600
block set 55 9 0 Stone
block set 57 9 0 Stone
block set 56 9 -1 Stone
block set 56 9 1 Stone
geology force eruption 1 12
teleport 48 14 0 1.570796 -0.45
let basin_id = geology.id
assert geology.position.x == 56
geology step 72
geology stop ${basin_id}
geology step 1500
assert geology.solidified >= 1 && geology.volume_error == 0
teleport 56 9.01 0 1.570796 -0.25
assert geology.cell_volume == 0 && geology.cell_collision_height == 1
save
load
assert geology.solidified >= 1 && geology.volume_error == 0
assert geology.cell_collision_height == 1
teleport 48 14 0 1.570796 -0.45
pause off
screenshot
pause on
exit 0
