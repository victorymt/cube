terrain varied
start
wait until game.screen == "playing" timeout 7200
wait until stream.surface_ready timeout 1800
teleport -2040 22 -2067 0 -0.6
stream wait 900
teleport -2040 22 -2067 0 -0.6
screenshot
status
input manual
