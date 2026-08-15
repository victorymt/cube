#!/bin/sh
set -eu

fail()
{
    echo "architecture check failed: $1" >&2
    exit 1
}

main_lines=$(wc -l < src/main.c)
[ "$main_lines" -le 8 ] || fail "src/main.c must remain a tiny entry point"
grep -q 'return GameRun(argc, argv);' src/main.c ||
    fail "src/main.c must delegate to GameRun"

if grep -R -n '#[[:space:]]*pragma[[:space:]]*weak' src --include='*.c' --include='*.h'; then
    fail "production code must not use weak symbols"
fi

if grep -n '#[[:space:]]*include[[:space:]]*"fluid.h"' src/world.c src/chunks.c; then
    fail "world and chunks must use extension hooks instead of fluid includes"
fi

if grep -nE 'extern .*(terrainMode|ForHud|shipHud)' src/*.h; then
    fail "mutable terrain and HUD state must not be public externs"
fi

if grep -R -n '^TerrainMode terrainMode[[:space:]]*=' src --include='*.c'; then
    fail "terrain mode ownership must remain inside world.c"
fi

if grep -nE '^(float|bool|int|BlockType|char) (dayTimeForHud|autoSaveForHud|blockForHud|SpaceEditCountForHud|shipSpeedForHud|shipHud)' src/render.c; then
    fail "render HUD state must be passed through frame structs"
fi

grep -q 'GameUpdateInteractions(' src/game.c ||
    fail "game frame updates must delegate world interactions"
if grep -nE 'RaycastBlocksFiltered|FluidTryCollectUnit|FluidTryDepositUnit|ShipTryEnter' src/game.c; then
    fail "world interaction mechanics must remain in game_interaction.c"
fi

grep -q 'GameDispatchDebugCommand(&game)' src/game.c ||
    fail "game loop must delegate debug command dispatch"
if grep -n 'DebugControlPoll' src/game.c; then
    fail "debug command routing must remain in game_debug.c"
fi

render_frame_body=$(sed -n '/^static void GameRenderFrame(/,/^}/p' src/game.c)
for render_stage in \
    GameRenderBackground \
    GameRenderWorldPass \
    GameBuildShipHud \
    GameRenderEnvironmentOverlays \
    GameRenderHud \
    GameRenderPauseOverlay
do
    printf '%s\n' "$render_frame_body" | grep -q "$render_stage(" ||
        fail "game rendering must delegate to $render_stage"
done
render_frame_lines=$(printf '%s\n' "$render_frame_body" | wc -l)
[ "$render_frame_lines" -le 25 ] ||
    fail "GameRenderFrame must remain a render-stage coordinator"

echo "architecture checks passed"
