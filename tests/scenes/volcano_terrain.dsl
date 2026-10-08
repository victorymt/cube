terrain varied
start
pause on
surface debug planet desert 1234
view distance 12
weather force clear 1 36000
geology find
assert geology.terrain_found
let center = geology.terrain_position
let radius = geology.terrain_radius
let crater = geology.terrain_crater_radius
teleport ${center.x} ${center.y+45} ${center.z} 0 -0.8
stream wait 1200
let floor = geology.terrain_ground
assert floor == center.y-1
teleport ${center.x+crater} ${center.y+60} ${center.z} 0 -0.8
assert geology.terrain_ground > floor+12
teleport ${center.x-24+0.5} ${center.y+45} ${center.z+0.5} 1.570796 -0.5
stream wait 1200
geology step 4
assert geology.present && geology.active
assert geology.position.y == center.y
let id = geology.id
geology force eruption 1 240
assert geology.id == id && geology.phase == "unrest"
geology step 80
assert geology.phase == "erupting" && geology.supplied > 0
save
load
assert geology.id == id && geology.phase == "erupting"
weather force clear 1 36000
teleport ${center.x-radius*1.3} ${center.y+60} ${center.z} 1.570796 -0.20
stream wait 1200
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 1200
pause on
teleport ${center.x-radius*1.3} ${center.y+60} ${center.z} 1.570796 -0.20
stream wait 1200
pause off
screenshot
pause on
teleport ${center.x-60} ${center.y+85} ${center.z+40} 2.158799 -0.75
stream wait 1200
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 1200
pause on
teleport ${center.x-60} ${center.y+85} ${center.z+40} 2.158799 -0.75
stream wait 1200
pause off
screenshot
pause on
exit 0
