terrain varied
start
wait until game.screen == "playing" timeout 7200
wait until stream.surface_ready timeout 1800
view distance 12
weather clear
weather cloud clear
world time 0.5
teleport 7680 90 3840 0 0
stream wait 900
input 0 0 0 0 240
wait until player.input_frames == 0 timeout 1200
teleport 7680 106 3845 3.141593 0.02
screenshot
status
input manual
