# Deterministic visual probe for the world-surface render debug modes.
assert game.screen == "start" && !settings.autosave
terrain flat
start
wait until game.screen == "playing" timeout 7200
wait until stream.surface_ready timeout 1800
weather force clear 0.0 36000
weather damage off
world time 0.5
view distance 1
stream wait 600
view first
look 0 -0.4
render debug final
assert render.debug_mode == "final"
screenshot
render debug albedo
assert render.debug_mode == "albedo"
screenshot
render debug normal
assert render.debug_mode == "normal"
screenshot
render debug ao
assert render.debug_mode == "ao"
screenshot
render debug diffuse
assert render.debug_mode == "diffuse"
screenshot
render debug specular
assert render.debug_mode == "specular"
screenshot
render debug shadow
assert render.debug_mode == "shadow"
screenshot
render debug off
assert render.debug_mode == "final"
exit 0
