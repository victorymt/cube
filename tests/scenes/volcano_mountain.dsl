# Playable 3D H0 demonstration. Intentionally no exit: explore after it builds.
terrain flat
start
view distance 8
weather force clear 1 36000
teleport 0 30 0 0 -0.3
input 0 0 0 0 240
wait until player.input_frames == 0 timeout 1200
pause on
teleport 0 30 0 0 -0.3
stream wait 1200
geology deposition mountain
assert geology.deposition_ready
assert geology.deposition_supplied > 1000
assert geology.deposition_liquid > 0 && geology.deposition_solid > 0
assert geology.deposition_volume_error > -1e-6 && geology.deposition_volume_error < 1e-6
let lava = geology.lava_volume
save
load
assert geology.lava_volume == lava
weather force clear 1 36000
pause off
teleport -38 35 -38 0.785398 -0.30
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 1200
pause on
teleport -38 35 -38 0.785398 -0.30
stream wait 1200
pause off
screenshot
