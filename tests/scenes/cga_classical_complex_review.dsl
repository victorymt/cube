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
cga inspect

# 1. Forecourt: paifang frame looking through the gatehouse.
teleport 7680 100 3885 0 0
look at 7680 104 3858
cga visible
screenshot

# 2. Inner courtyard: two-storey main hall.
teleport 7680 106 3845 0 0
look at 7680 104 3820
cga visible
screenshot

# 3. Courtyard looking south: gatehouse, screen wall and corridors.
teleport 7680 104 3838 0 0
look at 7680 100 3858
cga visible
screenshot

# 4. Upper roof close: hip ridges, beast groups and tiles.
teleport 7695 112 3838 0 0
look at 7680 110 3815
cga visible
screenshot

input manual
