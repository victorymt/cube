# Cube-native home trees must be generated as flora structures rather than
# retained in the opaque terrain mesh. The address below is a deterministic
# Scots Pine root in the default Homeworld seed's forest region.
assert game.screen == "start"
terrain varied
start
wait until game.screen == "playing" timeout 7200
surface debug cube
view distance 4
wait until world.surface_topology == "cube-grid" timeout 1800
wait until stream.cube.active timeout 1800
wait until stream.surface_ready timeout 1800
pause on
surface address 0 1884 1762 12 0 -0.25
pause off
wait until stream.surface_ready timeout 1800
wait until player.velocity.y == 0 timeout 600
flora near 64
assert flora.near_active
assert flora.near_count > 0
assert flora.near_nearest_kind == "home_tree"
assert flora.near_nearest_taxon == "Scots Pine"
let tree = flora.near_nearest_root
let camera = tree + vec3(-16, 9, 0)
fly on
teleport ${camera.x} ${camera.y} ${camera.z} 0 0
look at ${tree.x} ${tree.y + 10} ${tree.z}
input 0 0 0 0 5
wait until player.input_frames == 0 timeout 120
wait until stream.surface_ready timeout 1800
render pick 640 360
assert render.pick.hit
screenshot
exit 0
