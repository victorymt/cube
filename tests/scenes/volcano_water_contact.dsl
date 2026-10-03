terrain flat
start
teleport 161 14 0 1.570796 -0.25
stream wait 600
pause on
# Contain a small lava parcel, then expose one face to a finite water pool.
block set 184 9 0 Stone
block set 186 9 0 Stone
block set 185 9 -1 Stone
block set 185 9 1 Stone
block set 185 10 0 Stone
block set 187 9 0 Stone
block set 186 9 -1 Stone
block set 186 9 1 Stone
block set 186 10 0 Stone
geology force eruption 1 120
let vent = geology.id
geology step 60
geology step 1
geology stop ${vent}
assert geology.supplied == 0.125
block set 186 9 0 Air
fluid set 186 9 0 255
fluid step 5
geology step 1
assert geology.evaporated_units > 0 && geology.reservoir_units == 0
teleport 186.1 9.01 0.1 0 0
assert fluid.volume + geology.evaporated_units == 255
let saved_water = fluid.volume
let saved_evaporated = geology.evaporated_units
teleport 185.1 9.01 0.1 0 0
let saved_heat = geology.cell_temperature
let saved_credit = geology.cell_contact_credit
assert saved_credit > 0 && geology.cell_steam > 0
save
geology step 8
let expected_heat = geology.cell_temperature
let expected_evaporated = geology.evaporated_units
load
stream wait 600
assert geology.cell_temperature == saved_heat
assert geology.cell_contact_credit == saved_credit
assert geology.cell_steam == 0
assert geology.evaporated_units == saved_evaporated
geology step 8
assert geology.cell_temperature == expected_heat
assert geology.evaporated_units == expected_evaporated
teleport 186.1 9.01 0.1 0 0
assert fluid.volume + geology.evaporated_units == 255
# An unloaded vent parks both heat and reaction progress, with no catch-up.
block set 185 10 0 Air
teleport 182 12 5 2.5 -0.45
pause off
screenshot
pause on
teleport 185.1 9.01 0.1 0 0
let parked_heat = geology.cell_temperature
let parked_credit = geology.cell_contact_credit
let parked_water = geology.evaporated_units
teleport 1000 14 0 0 0
stream wait 600
geology step 20
assert !geology.active && geology.evaporated_units == parked_water
teleport 185.1 9.01 0.1 0 0
stream wait 600
assert geology.cell_temperature == parked_heat
assert geology.cell_contact_credit == parked_credit
geology step 1500
assert geology.volume_error == 0
assert geology.energy_error > -0.01 && geology.energy_error < 0.01
assert geology.cell_liquid == 0 && geology.cell_volume == 0.125
assert geology.cell_collision_height == 0.125
teleport 186.1 9.01 0.1 0 0
assert fluid.volume > 0 && fluid.volume + geology.evaporated_units == 255
save
load
stream wait 600
assert fluid.volume + geology.evaporated_units == 255
# A second sealed basin contains a full lava block and only four water units.
teleport 193 14 0 1.570796 -0.25
stream wait 600
block set 216 9 0 Stone
block set 218 9 0 Stone
block set 217 9 -1 Stone
block set 217 9 1 Stone
block set 217 10 0 Stone
block set 219 9 0 Stone
block set 218 9 -1 Stone
block set 218 9 1 Stone
geology force eruption 1 120
teleport 209 14 0 1.570796 -0.25
let full_vent = geology.id
assert geology.position.x == 217
geology step 68
geology stop ${full_vent}
teleport 217.1 9.01 0.1 0 0
assert geology.cell_volume == 1
block set 217 10 0 Air
block set 218 9 0 Air
fluid set 218 9 0 4
fluid step 5
let evaporated_before = geology.evaporated_units
geology step 5
assert geology.cell_steam > 0
teleport 214 12 5 2.5 -0.45
pause off
screenshot
pause on
geology step 1500
teleport 218.1 9.01 0.1 0 0
assert fluid.volume == 0
assert geology.evaporated_units == evaporated_before + 4
assert geology.solidified >= 1 && geology.volume_error == 0
assert geology.energy_error > -0.01 && geology.energy_error < 0.01
teleport 217.1 9.01 0.1 0 0
assert geology.cell_volume == 0 && geology.cell_collision_height == 1
save
load
stream wait 600
assert geology.cell_collision_height == 1
exit 0
