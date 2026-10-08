# H0 sandbox fixtures: this scene does not create or mutate a game world.
assert !geology.deposition_ready
geology deposition test slope
assert geology.deposition_ready && geology.deposition_supplied == 1
assert geology.deposition_outlet > 0.99
assert geology.deposition_volume_error > -1e-9 && geology.deposition_volume_error < 1e-9
assert geology.deposition_energy_error > -1e-7 && geology.deposition_energy_error < 1e-7
geology deposition test basin
assert geology.deposition_ready && geology.deposition_pool == 0.5
assert geology.deposition_outlet == 0 && geology.deposition_retained == 0.5
geology deposition test overflow
assert geology.deposition_ready && geology.deposition_supplied == 4.5
assert geology.deposition_outlet > 2
assert geology.deposition_volume_error > -1e-9 && geology.deposition_volume_error < 1e-9
assert geology.deposition_energy_error > -1e-7 && geology.deposition_energy_error < 1e-7
geology deposition test cooling
assert geology.deposition_ready && geology.deposition_liquid == 0
assert geology.deposition_solid > 0.999999999 && geology.deposition_solid < 1.000000001
assert geology.deposition_volume_error > -1e-9 && geology.deposition_volume_error < 1e-9
assert geology.deposition_energy_error > -1e-7 && geology.deposition_energy_error < 1e-7
exit 0
