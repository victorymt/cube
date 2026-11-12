# Cube collision must use the block catalog's collision height. Cross plants
# and other zero-height decorations are passable; a full stone block still
# stops the same movement. The unit test enumerates every zero-height family;
# this replay covers the real scripted input and block-edit path.
assert game.screen == "start"
terrain flat
start
wait until game.screen == "playing" timeout 7200
surface debug cube
wait until stream.cube.active timeout 1800
wait until stream.surface_ready timeout 1800
fly on

teleport 0 20 0 0 0
block set 0 20 2 Flower
cell inspect 0 20 2
assert cell.block == "Flower"
input 1 0 0 0 60
wait until player.input_frames == 0 timeout 180
assert player.position.z > 4.0

teleport 0 20 0 0 0
block set 0 20 2 Tall_Grass
cell inspect 0 20 2
assert cell.block == "Tall Grass"
input 1 0 0 0 60
wait until player.input_frames == 0 timeout 180
assert player.position.z > 4.0

teleport 0 20 0 0 0
block set 0 20 2 Stone
cell inspect 0 20 2
assert cell.block == "Stone"
input 1 0 0 0 60
wait until player.input_frames == 0 timeout 180
assert player.position.z < 1.5

block set 0 20 2 Air
exit 0
