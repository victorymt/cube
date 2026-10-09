terrain varied
start
pause on
surface debug planet desert 1234
geology enabled off
geology find
assert geology.terrain_found
geology history
assert geology.history_ready
assert geology.history_supplied > 1000
assert geology.history_flank > 100
assert geology.history_foot > 1
assert geology.history_boundary < 0.000001
assert geology.history_volume_error > -0.00001 && geology.history_volume_error < 0.00001
assert geology.history_energy_error > -0.01 && geology.history_energy_error < 0.01
assert geology.history_terminal_cooling > 0
exit 0
