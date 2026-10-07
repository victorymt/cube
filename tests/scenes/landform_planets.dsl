terrain varied
start
pause on
assert landform.available
assert landform.strategy == "homeworld"
surface debug planet temperate 1234
assert landform.available
assert landform.strategy == "water_world"
assert landform.surface == "planet"
assert landform.capabilities == 7
assert !landform.coriolis_enabled
landform find river 8192
assert landform.found
let river = landform.position
teleport ${river.x} ${river.y} ${river.z} 0 -0.6
stream wait 1200
landform inspect
assert landform.kind == "river"
assert landform.has_water
landform inspect
let bed = landform.elevation
let water = landform.water_level
let deflection = landform.deflection_blocks
let body = landform.body
save
teleport 0 150 0 0 0
load
stream wait 1200
assert landform.body == body
assert landform.kind == "river"
assert landform.elevation == bed
assert landform.water_level == water
assert landform.deflection_blocks == deflection
surface debug planet desert 1234
assert !landform.available
surface debug planet lava 1234
assert !landform.available
surface debug planet ice 1234
assert !landform.available
surface debug planet temperate 4321
assert landform.available
assert landform.body != body
surface debug planet temperate 1234
teleport ${river.x} ${river.y} ${river.z} 0 -0.6
stream wait 1200
assert landform.body == body
assert landform.elevation == bed
assert landform.water_level == water
assert landform.deflection_blocks == deflection
surface debug home
assert landform.strategy == "homeworld"
assert !landform.coriolis_enabled
exit 0
