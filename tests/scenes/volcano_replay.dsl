terrain flat
start
teleport 0 14 0 1.570796 -0.15
stream wait 600
pause on
assert geology.records == 0
geology inspect
assert geology.records == 0
geology force eruption 1 120
assert geology.present && geology.active && geology.phase == "unrest"
assert geology.event == 1 && geology.records == 1
let vent_id = geology.id
let vent_position = geology.position
let reservoir = geology.reservoir
geology step 60
assert geology.phase == "erupting"
assert geology.ticks == 60 && geology.reservoir == reservoir
save
let saved_ticks = geology.ticks
geology step 10
assert geology.ticks == saved_ticks + 10
load
assert geology.id == vent_id && geology.ticks == saved_ticks
assert geology.phase == "erupting" && geology.event == 1
geology inspect
teleport 0 14 0 1.570796 -0.25
pause off
screenshot
pause on
let ticks_before_parking = geology.ticks
teleport 1000 14 0 1.570796 -0.25
geology step 20
assert !geology.active && geology.ticks == ticks_before_parking
assert geology.id == vent_id && geology.event == 1
teleport 0 14 0 1.570796 -0.25
stream wait 600
geology step 1
assert geology.active && geology.ticks == ticks_before_parking + 1
geology stop ${vent_id}
assert geology.phase == "cooling"
assert geology.reservoir == reservoir
geology step 120
assert geology.phase == "recovery"
geology force eruption 0.5 10
assert geology.event == 2 && geology.id == vent_id
geology vents
exit 0
