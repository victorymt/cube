terrain flat
start
teleport 161 14 0 1.570796 -0.25
stream wait 600
pause on
weather force clear 1 36000
geology force eruption 1 120
let vent = geology.id
geology stop ${vent}
teleport 188 14 0 0 0
stream wait 600
block set 177 9 0 Stone
block set 179 9 0 Stone
block set 178 9 -1 Stone
block set 178 9 1 Stone
block set 178 10 0 Stone
block set 181 9 0 Stone
block set 183 9 0 Stone
block set 182 9 -1 Stone
block set 182 9 1 Stone
block set 182 10 0 Stone
block set 189 9 0 Stone
block set 191 9 0 Stone
block set 190 9 -1 Stone
block set 190 9 1 Stone
block set 190 10 0 Stone
block set 193 9 0 Stone
block set 195 9 0 Stone
block set 194 9 -1 Stone
block set 194 9 1 Stone
block set 194 10 0 Stone
block set 197 9 0 Stone
block set 199 9 0 Stone
block set 198 9 -1 Stone
block set 198 9 1 Stone
block set 198 10 0 Stone
geology deposit basalt 178 9 0 1024
geology deposit silicic 182 9 0 1024
geology deposit silicic 190 9 0 1024
geology deposit scoria 194 9 0 1024
geology deposit pumice 198 9 0 1024
block set 192 9 0 Stone
block set 191 9 -1 Stone
block set 191 9 1 Stone
block set 191 10 0 Stone
block set 191 9 0 Air
fluid set 191 9 0 255
fluid step 5
geology step 6
teleport 190.1 9.01 0.1 0 0
assert geology.cell_silica == 1 && geology.cell_vesicles == 0
assert geology.cell_quenched > 0
let saved_quenched = geology.cell_quenched
save
geology step 120
assert geology.cell_rock == "obsidian" && geology.cell_collision_height == 1
let expected_evaporated = geology.evaporated_units
load
stream wait 600
assert geology.cell_quenched == saved_quenched
geology step 120
assert geology.evaporated_units == expected_evaporated
teleport 178.1 9.01 0.1 0 0
assert geology.cell_rock == "basalt" && geology.cell_collision_height == 1
assert geology.cell_volume == 0
block set 178 10 0 Air
teleport 182.1 9.01 0.1 0 0
assert geology.cell_rock == "rhyolite" && geology.cell_collision_height == 1
assert geology.cell_volume == 0
block set 182 10 0 Air
teleport 190.1 9.01 0.1 0 0
assert geology.cell_rock == "obsidian" && geology.cell_collision_height == 1
assert geology.cell_volume == 0
block set 190 10 0 Air
teleport 194.1 9.01 0.1 0 0
assert geology.cell_rock == "scoria" && geology.cell_collision_height == 1
assert geology.cell_volume == 0
block set 194 10 0 Air
teleport 198.1 9.01 0.1 0 0
assert geology.cell_rock == "pumice" && geology.cell_collision_height == 1
assert geology.cell_volume == 0
block set 198 10 0 Air
assert geology.solidified == 5 && geology.volume_error == 0
assert geology.energy_error > -0.01 && geology.energy_error < 0.01
save
load
stream wait 600
assert geology.cell_rock == "pumice"
teleport 188 17 9 3.141593 -0.65
pause off
screenshot
pause on
exit 0
