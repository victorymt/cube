terrain varied
start
pause on
surface debug planet desert 1234
geology enabled on
weather force clear 1 36000
geology find
assert geology.terrain_found
let center = geology.terrain_position
teleport ${center.x-24+0.5} ${center.y+5} ${center.z+0.5} 1.570796 -0.6
stream wait 1200
geology step 4
geology force eruption 1 1800
geology step 120
geology inspect
let vent = geology.position
teleport ${vent.x} ${vent.y+1.5} ${vent.z} 0 -0.5
pause off
input 0 0 0 0 30
wait until player.input_frames == 0 timeout 1200
pause on
geology inspect
assert geology.heat_exposure > 0 && geology.heat_damage > 0 && geology.heat_sources > 0
assert geology.heat_contact == false || geology.heat_exposure > 0
let damage = geology.heat_damage
weather damage off
pause off
input 0 0 0 0 30
wait until player.input_frames == 0 timeout 1200
pause on
assert geology.heat_exposure > 0 && geology.heat_damage == damage
weather damage on
save
load
stream wait 1200
assert geology.heat_damage == damage
exit 0
