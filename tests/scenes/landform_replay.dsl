terrain varied
start
pause on
assert landform.available
assert landform.version == 7
landform find river 4096
assert landform.found
let river = landform.position
teleport ${river.x} ${river.y} ${river.z} 0 -0.6
stream wait 1200
landform inspect
assert landform.kind == "river"
assert landform.has_water
let bed = landform.elevation
let water = landform.water_level
pause off
screenshot
pause on
save
teleport 0 150 0 0 0
load
stream wait 1200
assert landform.kind == "river"
assert landform.elevation == bed
assert landform.water_level == water
landform find lake 4096
assert landform.found
let lake = landform.position
teleport ${lake.x} ${lake.y} ${lake.z} 0 -0.6
stream wait 1200
landform inspect
assert landform.kind == "lake"
assert landform.has_water
pause off
screenshot
pause on
landform find dry_channel 4096
assert landform.found
let dry = landform.position
teleport ${dry.x} ${dry.y} ${dry.z} 0 -0.6
stream wait 1200
landform inspect
assert landform.kind == "dry_channel"
assert !landform.has_water
pause off
screenshot
exit
