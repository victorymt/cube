# Fixed-position flat-world camera route: two full turns at one degree/frame.
assert game.screen == "start" && !settings.autosave
terrain flat
start
wait until game.screen == "playing" timeout 7200
wait until stream.surface_ready timeout 1800
wait until !ecology.catchup_pending timeout 600
weather force clear 0.0 36000
weather damage off
view first
look 0 -0.35
wait until player.velocity.y == 0 timeout 600
let anchor = player.position
stream wait 600
assert player.position == anchor
repeat 720 {
    look delta 0.01745329252 0
}
assert player.position == anchor
exit 0
