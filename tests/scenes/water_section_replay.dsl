# Activate water at the bottom of the loaded window before descending.
# The section below must remain unknown until terrain streaming generates it.
terrain varied
start
pause on
teleport 226 95 276 5.340708 0.034907
stream wait 1800
fluid inspect 226 64 276
fluid inspect 226 63 276
fluid set 226 64 276 255
fluid step 30
fluid inspect 226 63 276
teleport 226 55 276 5.340708 0.034907
stream wait 1800
pause off
teleport 226 55 276 5.340708 0.034907
assert stream.surface_ready
assert water.eyes_submerged
assert fluid.volume == 255
status
screenshot
save
load
stream wait 1800
teleport 226 55 276 5.340708 0.034907
assert stream.surface_ready
assert water.eyes_submerged
assert fluid.volume == 255
status
screenshot
exit 0
