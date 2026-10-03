terrain flat
start
teleport 0 14 0 1.570796 -0.15
stream wait 600
pause on
geology force eruption 1 120
let first_id = geology.id
geology step 61
assert geology.supplied > 0 && geology.hot_cells > 0
geology stop ${first_id}
# Keep the second vent loaded while the first vent and its lava are parked.
teleport 161 14 0 1.570796 -0.15
stream wait 600
geology force eruption 1 120
let second_id = geology.id
assert second_id != first_id && geology.position.x == 185
geology stop ${second_id}
let parked_heat = geology.energy_error
let parked_volume = geology.lava_volume
geology step 1500
geology vents
assert geology.active_count == 1 && geology.active && geology.id == second_id
assert geology.phase == "recovery"
assert geology.hot_cells > 0 && geology.lava_volume == parked_volume
assert geology.volume_error == 0 && geology.energy_error == parked_heat
# Returning to the first vent resumes its cooling and permits recovery too.
teleport 0 14 0 1.570796 -0.15
stream wait 600
geology step 1500
assert geology.id == first_id && geology.phase == "recovery"
assert geology.hot_cells == 0 && geology.volume_error == 0
exit 0
