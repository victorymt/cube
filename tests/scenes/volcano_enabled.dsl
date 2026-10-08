assert geology.enabled
geology enabled off
assert !geology.enabled
terrain flat
start
teleport 0 14 0 1.570796 -0.25
stream wait 600
pause on
assert !geology.enabled && geology.records == 0 && geology.lava_ticks == 0
geology enabled on
weather damage off
geology force eruption 1 120
let vent = geology.id
let position = geology.position
# Seed both fixed-step remainders with real frame time before freezing.
pause off
wait until geology.lava_accumulator > 0 && geology.vent_accumulator > 0 timeout 180
pause on
geology step 80
assert geology.phase == "erupting" && geology.supplied > 0 && geology.ash_supplied > 0
geology deposit ash 10 9 0 512
let ticks = geology.ticks
let lava_ticks = geology.lava_ticks
let ash_ticks = geology.ash_ticks
let reservoir = geology.reservoir
let lava = geology.lava_volume
let ash = geology.ash_volume
let supplied = geology.supplied
let lava_remainder = geology.lava_accumulator
let vent_remainder = geology.vent_accumulator
teleport ${position.x+0.1} ${position.y+0.01} ${position.z+0.1} 0 0
let temperature = geology.cell_temperature
let volume = geology.cell_volume
assert temperature > 373 && volume > 0
geology enabled off
geology step 100
assert geology.cell_temperature == temperature && geology.cell_volume == volume
assert geology.ticks == ticks && geology.lava_ticks == lava_ticks && geology.ash_ticks == ash_ticks
assert geology.lava_accumulator == lava_remainder && geology.vent_accumulator == vent_remainder
assert geology.reservoir == reservoir && geology.supplied == supplied
# Ordinary simulation must also remain frozen, even with weather damage on.
weather damage on
teleport 0 14 0 1.570796 -0.25
pause off
input 0 0 0 0 120
wait until player.input_frames == 0 timeout 180
screenshot
pause on
assert !geology.enabled && geology.ticks == ticks
assert geology.lava_ticks == lava_ticks && geology.ash_ticks == ash_ticks
assert geology.lava_volume == lava && geology.ash_volume == ash
assert geology.lava_accumulator == lava_remainder && geology.vent_accumulator == vent_remainder
save
load
assert !geology.enabled && geology.id == vent && geology.ticks == ticks
assert geology.lava_volume == lava && geology.ash_volume == ash
assert geology.lava_accumulator == lava_remainder && geology.vent_accumulator == vent_remainder
stream wait 600
teleport 10.1 9.01 0.1 0 0
assert geology.cell_ash_height == 0.5 && geology.cell_collision_height == 0.5
teleport ${position.x+0.1} ${position.y+0.01} ${position.z+0.1} 0 0
assert geology.cell_temperature == temperature && geology.cell_volume == volume
geology enabled on
weather damage off
geology step 1
assert geology.ticks == ticks+1 && geology.lava_ticks == lava_ticks+5
assert geology.ash_ticks == ash_ticks+5 && geology.supplied > supplied
assert geology.volume_error == 0 && geology.ash_volume_error == 0
geology enabled off
teleport 0 14 0 1.570796 -0.25
screenshot
exit 0
