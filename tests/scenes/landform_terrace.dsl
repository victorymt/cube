terrain varied
start
pause on
assert landform.version == 8
landform find terrace 4096
assert landform.found
let terrace = landform.position
teleport ${terrace.x} ${terrace.y} ${terrace.z} 0 -0.6
stream wait 1200
landform inspect
assert landform.kind == "terrace"
assert !landform.has_water
assert landform.sediment > 0
let elevation = landform.elevation
save
teleport 0 150 0 0 0
load
stream wait 1200
assert landform.version == 8
assert landform.kind == "terrace"
assert landform.elevation == elevation
pause off
screenshot
teleport ${terrace.x} ${terrace.y + 30} ${terrace.z} 0 -1.2
stream wait 1200
screenshot
exit
