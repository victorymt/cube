terrain varied
start
pause on
hud debug off
surface debug planet temperate 1234
weather force clear 0 36000
assert landform.available
landform find mouth 8192
assert landform.found
let mouth_a = landform.position
let mouth_a_y = mouth_a.y + 24
teleport ${mouth_a.x} ${mouth_a_y} ${mouth_a.z} 0 -1.1
# Allow normal view-distance streaming before restoring the survey camera.
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 600
pause on
teleport ${mouth_a.x} ${mouth_a_y} ${mouth_a.z} 0 -1.1
stream wait 1200
assert landform.is_mouth
landform inspect
pause off
screenshot
pause on
landform find lake 8192
assert landform.found
let lake_a = landform.position
let lake_a_y = lake_a.y + 24
teleport ${lake_a.x} ${lake_a_y} ${lake_a.z} 0 -1.1
# Allow normal view-distance streaming before restoring the survey camera.
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 600
pause on
teleport ${lake_a.x} ${lake_a_y} ${lake_a.z} 0 -1.1
stream wait 1200
assert landform.kind == "lake"
landform inspect
pause off
screenshot
pause on
surface debug planet temperate 4321
weather force clear 0 36000
landform find mouth 8192
assert landform.found
let mouth_b = landform.position
let mouth_b_y = mouth_b.y + 24
teleport ${mouth_b.x} ${mouth_b_y} ${mouth_b.z} 0 -1.1
# Allow normal view-distance streaming before restoring the survey camera.
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 600
pause on
teleport ${mouth_b.x} ${mouth_b_y} ${mouth_b.z} 0 -1.1
stream wait 1200
assert landform.is_mouth
landform inspect
pause off
screenshot
pause on
landform find lake 8192
assert landform.found
let lake_b = landform.position
let lake_b_y = lake_b.y + 24
teleport ${lake_b.x} ${lake_b_y} ${lake_b.z} 0 -1.1
# Allow normal view-distance streaming before restoring the survey camera.
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 600
pause on
teleport ${lake_b.x} ${lake_b_y} ${lake_b.z} 0 -1.1
stream wait 1200
assert landform.kind == "lake"
landform inspect
pause off
screenshot
pause on
surface debug planet temperate 991
weather force clear 0 36000
landform find mouth 8192
assert landform.found
let mouth_c = landform.position
let mouth_c_y = mouth_c.y + 24
teleport ${mouth_c.x} ${mouth_c_y} ${mouth_c.z} 0 -1.1
# Allow normal view-distance streaming before restoring the survey camera.
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 600
pause on
teleport ${mouth_c.x} ${mouth_c_y} ${mouth_c.z} 0 -1.1
stream wait 1200
assert landform.is_mouth
landform inspect
pause off
screenshot
pause on
landform find lake 8192
assert landform.found
let lake_c = landform.position
let lake_c_y = lake_c.y + 24
teleport ${lake_c.x} ${lake_c_y} ${lake_c.z} 0 -1.1
# Allow normal view-distance streaming before restoring the survey camera.
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 600
pause on
teleport ${lake_c.x} ${lake_c_y} ${lake_c.z} 0 -1.1
stream wait 1200
assert landform.kind == "lake"
landform inspect
pause off
screenshot
pause on
# Canonical seam and polar views use the same world as the last two captures.
teleport 8191.5 200 0.5 0 -1.2
let dateline_height = landform.elevation
let dateline_kind = landform.kind
let dateline_y = landform.elevation + 32
teleport 8191.5 ${dateline_y} 0.5 0 -1.2
# Allow normal view-distance streaming before restoring the survey camera.
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 600
pause on
teleport 8191.5 ${dateline_y} 0.5 0 -1.2
stream wait 1200
landform inspect
pause off
screenshot
pause on
teleport -8192.5 ${dateline_y} 0.5 0 -1.2
assert landform.elevation == dateline_height
assert landform.kind == dateline_kind
# Longitude pi/4 is the +X/+Z cube-face boundary at the equator.
teleport 2048.5 200 0.5 0 -1.2
let face_y = landform.elevation + 32
teleport 2048.5 ${face_y} 0.5 0 -1.2
# Allow normal view-distance streaming before restoring the survey camera.
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 600
pause on
teleport 2048.5 ${face_y} 0.5 0 -1.2
stream wait 1200
landform inspect
pause off
screenshot
pause on
teleport 0.5 200 4094.5 0 -1.2
let north_y = landform.elevation + 32
teleport 0.5 ${north_y} 4094.5 0 -1.2
# Allow normal view-distance streaming before restoring the survey camera.
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 600
pause on
teleport 0.5 ${north_y} 4094.5 0 -1.2
stream wait 1200
landform inspect
pause off
screenshot
pause on
teleport 0.5 200 -4094.5 0 -1.2
let south_y = landform.elevation + 32
teleport 0.5 ${south_y} -4094.5 0 -1.2
# Allow normal view-distance streaming before restoring the survey camera.
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 600
pause on
teleport 0.5 ${south_y} -4094.5 0 -1.2
stream wait 1200
landform inspect
pause off
screenshot
pause on
assert !landform.coriolis_enabled
# Control: identical pole views with ordinary landforms disabled by strategy.
surface debug planet desert 991
assert !landform.available
weather force clear 0 36000
teleport 0.5 220 4094.5 0 -1.2
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 600
pause on
teleport 0.5 220 4094.5 0 -1.2
stream wait 1200
assert !landform.available
pause off
screenshot
pause on
teleport 0.5 220 -4094.5 0 -1.2
pause off
input 0 0 0 0 180
wait until player.input_frames == 0 timeout 600
pause on
teleport 0.5 220 -4094.5 0 -1.2
stream wait 1200
assert !landform.available
pause off
screenshot
pause on
exit 0
