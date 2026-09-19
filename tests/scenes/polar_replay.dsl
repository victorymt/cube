# Keep the long polar input route independently reproducible at 1080p.
assert game.screen == "start" && !settings.autosave
start
wait until game.screen == "playing" timeout 7200
wait until stream.surface_ready timeout 1800
weather force cloudy 0.8 36000
weather cloud cumulonimbus 0.85 36000

teleport 123.5 130 4096.5 0 -0.35
wait until world.last_rebase && world.last_rebase_to.x > -8069 && world.last_rebase_to.x < -8068 && world.last_rebase_to.z == 4095.5 && world.last_rebase_north_sign == -1 timeout 120
wait until stream.surface_ready timeout 1800
assert world.north_pole_alias && world.latitude > 1.56 && world.duplicate_canonical_chunks == 0
assert weather.canonical_cell == ecology.canonical_cell && ecology.region_x == -127 && ecology.region_z == 63
status

teleport -321.5 130 -4096.5 0 -0.35
wait until world.last_rebase_to.x > 7870 && world.last_rebase_to.x < 7871 && world.last_rebase_to.z == -4095.5 && world.last_rebase_north_sign == -1 timeout 120
wait until stream.surface_ready timeout 1800
# Preserve the original 600 simulated input frames, with progress every 100.
repeat 6 {
    input 0 0 0 0 100
    wait until player.input_frames == 0 timeout 120
    assert world.canonical_position.x > 7870 && world.canonical_position.x < 7871 && world.canonical_position.z == -4095.5 && world.last_rebase_north_sign == -1 && world.latitude < -1.56 && world.south_pole_alias && world.duplicate_canonical_chunks == 0
    status
}
assert stream.surface_ready && player.input_frames == 0
assert weather.canonical_cell == ecology.canonical_cell && weather.canonical_cell.x == 7870 && weather.canonical_cell.z == -4096 && ecology.region_x == 122 && ecology.region_z == -64
screenshot
exit 0
