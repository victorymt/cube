terrain varied
start
wait until game.screen == "playing" timeout 7200
wait until stream.surface_ready timeout 1800
teleport 0 96 -64 0 -0.35
stream wait 600
screenshot
exit 0
