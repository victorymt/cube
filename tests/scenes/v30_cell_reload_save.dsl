# V30 complete-cell replay, phase 1: a planted cell carries water through
# block edits, undo/redo and an atomic save.
assert game.screen == "start"
terrain flat
start
wait until game.screen == "playing" timeout 7200
surface debug home
wait until stream.surface_ready timeout 1800
pause on
teleport 4 13 8 0 0
pause off
stream wait 1200
pause on
block set 4 12 8 Flower
fluid set 4 12 8 128
cell inspect 4 12 8
assert cell.known && cell.block == "Flower" && cell.water_volume > 0
let saved_volume = cell.water_volume
block set 4 12 8 Air
cell inspect 4 12 8
assert cell.block == "Air" && cell.water_volume == saved_volume
undo
cell inspect 4 12 8
assert cell.block == "Flower" && cell.water_volume == saved_volume
redo
cell inspect 4 12 8
assert cell.block == "Air" && cell.water_volume == saved_volume
undo
cell inspect 4 12 8
assert cell.block == "Flower" && cell.water_volume == saved_volume
save
exit 0
