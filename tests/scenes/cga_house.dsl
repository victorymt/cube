terrain varied
start
wait until game.screen == "playing" timeout 7200
wait until stream.surface_ready timeout 1800
teleport -1280 110 -2075 0 -0.35
stream wait 600
teleport -1280 110 -2075 0 -0.35
screenshot
input manual
