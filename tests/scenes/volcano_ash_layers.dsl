terrain flat
start
teleport 161 14 0 1.570796 -0.25
stream wait 600
pause on
weather force clear 1 36000
geology force eruption 1 120
let vent = geology.id
geology step 80
assert geology.ash_supplied > 0 && geology.ash_volume > 0
assert geology.reservoir + geology.supplied + geology.ash_supplied == 256
assert geology.ash_volume_error == 0
geology stop ${vent}
# Separate exposed and roofed layers, away from the fallout footprint.
geology deposit ash 151 9 0 512
geology deposit ash 153 9 0 512
block set 153 11 0 Stone
teleport 151.1 9.01 0.1 0 0
assert geology.cell_ash_height == 0.5 && geology.cell_collision_height == 0.5
let ticks = geology.ash_ticks
geology inspect
assert geology.ash_ticks == ticks && geology.cell_ash_height == 0.5
# Select only the occupied lower half of the voxel.
teleport 151.5 8.2 0.5 0 0
assert !target.hit
look 0 -1.4
assert target.hit && target.position.x == 151 && target.position.y == 9
assert target.block == "Volcanic Ash"
teleport 151.1 9.01 0.1 0 0
weather force heavy-rain 1 36000
geology step 12
assert geology.cell_ash_height < 0.5 && geology.ash_washed > 0
let saved_height = geology.cell_ash_height
let saved_washed = geology.ash_washed
save
geology step 30
let expected_height = geology.cell_ash_height
let expected_washed = geology.ash_washed
load
stream wait 600
assert geology.cell_ash_height == saved_height && geology.ash_washed == saved_washed
geology step 30
assert geology.cell_ash_height == expected_height && geology.ash_washed == expected_washed
teleport 153.1 9.01 0.1 0 0
assert geology.cell_ash_height == 0.5
# Removing support drops the whole finite layer into the hole.
weather force clear 1 36000
block set 153 8 0 Air
geology step 1
assert geology.cell_ash_height == 0
teleport 153.1 8.01 0.1 0 0
assert geology.cell_ash_height == 0.5 && geology.cell_collision_height == 0.5
# Rain cannot age parked layers while the player is elsewhere.
weather force heavy-rain 1 36000
let parked_volume = geology.ash_volume
teleport 1000 14 0 0 0
stream wait 600
geology step 50
assert geology.ash_volume == parked_volume
teleport 150 12 5 2.5 -0.5
stream wait 600
weather force clear 1 36000
assert geology.ash_volume_error == 0
pause off
screenshot
pause on
exit 0
