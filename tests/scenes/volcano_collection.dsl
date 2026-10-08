terrain flat
start
teleport 161 14 0 1.570796 -0.25
stream wait 600
pause on
weather force clear 1 36000
geology force eruption 1 120
let vent = geology.id
geology stop ${vent}
geology deposit ash 151 9 0 512
teleport 151.5 9.6 0.5 0 -1.4
assert target.block == "Volcanic Ash"
pause off
mouse left
wait until interaction.mouse_left_mesh_ready timeout 60
screenshot
pause on
assert geology.inventory_ash == 0 && geology.remainder_ash == 512
assert geology.ash_collected == 0.5 && geology.ash_volume == 0
save
geology deposit ash 151 9 0 512
teleport 151.5 9.6 0.5 0 -1.4
pause off
mouse left
wait until interaction.mouse_left_mesh_ready timeout 60
pause on
assert geology.inventory_ash == 1 && geology.remainder_ash == 0
load
stream wait 600
assert geology.inventory_ash == 0 && geology.remainder_ash == 512
assert geology.ash_collected == 0.5 && geology.ash_volume == 0
# A full inventory must preserve both source and the saved half-block.
geology deposit ash 151 9 0 512
geology inventory ash 999
teleport 151.5 9.6 0.5 0 -1.4
pause off
mouse left
wait until interaction.mouse_left_handle_frame >= interaction.mouse_left_press_frame timeout 60
pause on
assert interaction.mouse_left_set_block_frame == 0
assert geology.ash_volume == 0.5 && geology.remainder_ash == 512
geology inventory ash 0
pause off
mouse left
wait until interaction.mouse_left_mesh_ready timeout 60
pause on
assert geology.inventory_ash == 1 && geology.remainder_ash == 0
assert geology.ash_volume == 0 && geology.ash_collected == 1
# Mine a supporting normal block; the ash above must fall without duplication.
block set 151 9 0 Stone
geology deposit ash 151 10 0 256
teleport 149.5 8.1 0.5 1.570796 0
assert target.block == "Stone" && target.position.x == 151 && target.position.y == 9
pause off
mouse left
wait until interaction.mouse_left_mesh_ready timeout 120
pause on
geology step 1
teleport 151.1 9.01 0.1 0 0
assert geology.cell_ash_height == 0.25
weather force heavy-rain 1 36000
geology step 12
let remaining_ash = geology.ash_volume
let washed_ash = geology.ash_washed
assert remaining_ash < 0.25 && washed_ash > 0
weather force clear 1 36000
teleport 151.5 9.6 0.5 0 -1.4
pause off
mouse left
wait until interaction.mouse_left_mesh_ready timeout 60
pause on
assert geology.remainder_ash == remaining_ash*1024
assert geology.ash_volume == 0 && geology.ash_volume_error == 0
assert geology.ash_collected + geology.ash_washed == geology.ash_supplied
save
load
stream wait 600
assert geology.remainder_ash == remaining_ash*1024
assert geology.ash_washed == washed_ash && geology.ash_volume_error == 0
# Two distinct cooled thin rocks stay in separate material balances.
teleport 165 14 0 0 0
stream wait 600
block set 154 9 0 Stone
block set 156 9 0 Stone
block set 155 9 -1 Stone
block set 155 9 1 Stone
geology deposit pumice 155 9 0 512
block set 158 9 0 Stone
block set 160 9 0 Stone
block set 159 9 -1 Stone
block set 159 9 1 Stone
geology deposit basalt 159 9 0 512
geology step 2000
teleport 155.5 9.6 0.5 0 -1.4
assert target.block == "Pumice"
pause off
mouse left
wait until interaction.mouse_left_mesh_ready timeout 60
pause on
assert geology.inventory_pumice == 0 && geology.remainder_pumice == 512
teleport 159.5 9.6 0.5 0 -1.4
assert target.block == "Basalt"
pause off
mouse left
wait until interaction.mouse_left_mesh_ready timeout 60
pause on
assert geology.inventory_basalt == 0 && geology.remainder_basalt == 512
assert geology.collected == 1 && geology.lava_volume == 0 && geology.volume_error == 0
assert geology.energy_error > -0.01 && geology.energy_error < 0.01
save
load
stream wait 600
assert geology.remainder_pumice == 512 && geology.remainder_basalt == 512
assert geology.collected == 1 && geology.volume_error == 0
teleport 153 12 5 2.8 -0.5
pause off
screenshot
pause on
exit 0
