# Natural-scale playable volcano. No exit: explore when preparation finishes.
terrain varied
start
geology enabled off
surface debug planet desert 1234
view distance 12
weather force clear 1 36000
geology find
assert geology.terrain_found && geology.terrain_radius >= 110
assert geology.terrain_height >= 62
let center = geology.terrain_position
teleport ${center.x-60} ${center.y+10} ${center.z} 1.570796 -0.3
input 0 0 0 0 300
wait until player.input_frames == 0 timeout 1200
pause on
teleport ${center.x-60} ${center.y+10} ${center.z} 1.570796 -0.3
stream wait 1200
geology deposition mountain
assert geology.deposition_ready
assert geology.deposition_supplied > 1000
assert geology.deposition_liquid > 0 && geology.deposition_solid > 0
assert geology.deposition_volume_error > -1e-6 && geology.deposition_volume_error < 1e-6
assert geology.deposition_outlet < 0.001
assert geology.hot_cells <= 2048 && geology.lava_cells <= 8192
let lava = geology.lava_volume
save
load
assert geology.lava_volume == lava
weather force clear 1 36000
pause off
teleport ${center.x-105} ${center.y+25} ${center.z-65} 1.01649 -0.22
input 0 0 0 0 240
wait until player.input_frames == 0 timeout 1200
pause on
teleport ${center.x-105} ${center.y+25} ${center.z-65} 1.01649 -0.22
stream wait 1200
pause off
screenshot

# Explicit handoff: script EOF alone keeps automated sessions input-isolated.
input manual
assert player.manual_input
