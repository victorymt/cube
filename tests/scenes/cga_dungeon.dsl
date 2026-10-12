terrain varied
start
wait until game.screen == "playing" timeout 7200
wait until stream.surface_ready timeout 1800
teleport -1536 23 -2058 0 -0.8
stream wait 900
teleport -1536 23 -2058 0 -0.8
screenshot
status
input manual
