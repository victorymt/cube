terrain varied
start
stream wait 600
pause on
weather force clear 1 36000
weather cloud cumulus 0.8 36000
teleport 0 100 0 0 0.12
assert weather.cloud_layers > 0
let home_reference = weather.cloud_reference_y
let home_bottom = weather.cloud_bottom_y
assert home_reference == 80
weather inspect
pause off
screenshot
pause on
teleport 512 220 -128 0 -0.4
assert weather.cloud_reference_y == home_reference
assert weather.cloud_bottom_y == home_bottom
weather inspect
pause off
screenshot
pause on
teleport 513 140 -127 0 0.12
assert weather.cloud_reference_y == home_reference
assert weather.cloud_bottom_y == home_bottom
teleport 0 100 0 0 0.12
assert weather.cloud_bottom_y == home_bottom
surface debug planet temperate 12345
pause on
weather force clear 1 36000
weather cloud cumulus 0.8 36000
teleport 0 100 0 0 0.12
let planet_reference = weather.cloud_reference_y
let planet_bottom = weather.cloud_bottom_y
assert weather.cloud_layers > 0
weather inspect
pause off
screenshot
pause on
teleport 128 180 128 0 -0.4
assert weather.cloud_reference_y == planet_reference
assert weather.cloud_bottom_y == planet_bottom
weather inspect
pause off
screenshot
pause on
surface debug home
pause on
weather force clear 1 36000
weather cloud cumulus 0.8 36000
teleport 0 100 0 0 0.12
assert weather.cloud_reference_y == home_reference
assert weather.cloud_bottom_y == home_bottom
exit 0
