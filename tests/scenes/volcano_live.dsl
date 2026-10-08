# Natural terrain and live runtime only: no deposition snapshot or placed lava.
terrain varied
start
pause on
surface debug planet desert 1234
geology enabled on
view distance 12
weather force clear 1 36000
geology find
assert geology.terrain_found
let center = geology.terrain_position
let crater = geology.terrain_crater_radius
teleport ${center.x-24+0.5} ${center.y+5} ${center.z+0.5} 1.570796 -0.6
geology enabled off
pause off
input 0 0 0 0 300
wait until player.input_frames == 0 timeout 1200
pause on
teleport ${center.x-24+0.5} ${center.y+5} ${center.z+0.5} 1.570796 -0.6
stream wait 1200
geology enabled on
geology step 4
assert geology.present && geology.active && geology.reservoir == 4096
let id = geology.id
geology force eruption 1 1800
assert geology.id == id
geology step 360
geology inspect
assert geology.supplied > 200
assert geology.flow_radius > crater+5
assert geology.volume_error == 0
assert geology.solid_volume > 0
assert geology.hot_cells <= 2048 && geology.lava_cells <= 8192
let supplied = geology.supplied
let ticks = geology.ticks
let outlet = geology.position
save
load
weather force clear 1 36000
assert geology.supplied == supplied && geology.id == id
assert geology.position.y == outlet.y
stream wait 1200
geology step 20
assert geology.supplied >= supplied && geology.ticks == ticks+20
assert geology.volume_error == 0
teleport ${center.x-20} ${center.y+100} ${center.z+15} 2.2143 -1.30
geology enabled off
pause off
input 0 0 0 0 240
wait until player.input_frames == 0 timeout 1200
pause on
teleport ${center.x-20} ${center.y+100} ${center.z+15} 2.2143 -1.30
stream wait 1200
geology enabled on
pause off
screenshot
input manual
assert player.manual_input
