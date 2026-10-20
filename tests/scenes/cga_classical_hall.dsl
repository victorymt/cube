terrain varied
start
wait until game.screen == "playing" timeout 7200
wait until stream.surface_ready timeout 1800
teleport 7680 87 1160 3.141593 -0.05
stream wait 900
teleport 7680 87 1140 3.141593 -0.05
screenshot
status
input manual
