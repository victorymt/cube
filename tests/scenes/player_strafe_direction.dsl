# Positive strafe is screen-right, not map-axis-right. Verify yaw 0 and yaw 90
# so a future sign flip in RightFromYaw() cannot pass one orientation by luck.
assert game.screen == "start"
terrain flat
start
wait until game.screen == "playing" timeout 7200
wait until stream.surface_ready timeout 1800
fly on

teleport 0 20 0 0 0
input 0 0 0 0 5
wait until player.input_frames == 0 timeout 120
let zero_right_origin = player.position
input 0 1 0 0 30
wait until player.input_frames == 0 timeout 180
assert player.position.x < zero_right_origin.x - 2
assert player.position.z > zero_right_origin.z - 0.05
assert player.position.z < zero_right_origin.z + 0.05

teleport 0 20 0 0 0
input 0 0 0 0 5
wait until player.input_frames == 0 timeout 120
let zero_left_origin = player.position
input 0 -1 0 0 30
wait until player.input_frames == 0 timeout 180
assert player.position.x > zero_left_origin.x + 2

teleport 0 20 0 1.5707963268 0
input 0 0 0 0 5
wait until player.input_frames == 0 timeout 120
let north_right_origin = player.position
input 0 1 0 0 30
wait until player.input_frames == 0 timeout 180
assert player.position.z > north_right_origin.z + 2
exit 0
