#!/bin/sh
set -eu

fail()
{
    echo "architecture check failed: $1" >&2
    exit 1
}

for module in core world space ecology gameplay presentation app
do
    [ -d "src/$module" ] || fail "missing src/$module module"
done

[ -z "$(find src -maxdepth 1 -type f -print)" ] ||
    fail "production files must live in a named module"
[ ! -e src/core/types.h ] ||
    fail "core/types.h must stay decomposed into owned type headers"

main_lines=$(wc -l < src/app/main.c)
[ "$main_lines" -le 8 ] || fail "src/app/main.c must remain a tiny entry point"
grep -q 'return GameRun(argc, argv);' src/app/main.c ||
    fail "src/app/main.c must delegate to GameRun"

if grep -R -n '#[[:space:]]*pragma[[:space:]]*weak' src \
    --include='*.c' --include='*.h'; then
    fail "production code must not use weak symbols"
fi

unqualified_includes=$(
    grep -R -nE '#[[:space:]]*include[[:space:]]*"[^/"]+"' src \
        --include='*.c' --include='*.h' || true
)
if printf '%s\n' "$unqualified_includes" |
    grep -vE '"(raylib|raymath|rlgl)\.h"' | grep -q .; then
    printf '%s\n' "$unqualified_includes" >&2
    fail "project-local includes must use module-qualified paths"
fi

if grep -R -nE '#include "app/(app_types|game|game_debug|game_internal|game_interaction|game_landing|game_runtime)\.h"' \
    src/core src/world src/space src/ecology src/gameplay src/presentation; then
    fail "lower modules must not depend on app orchestration headers"
fi

public_externs=$(
    grep -R -n '^extern ' src --include='*.h' |
        grep -v '_internal\.h:' |
        grep -v 'extern const ' || true
)
[ -z "$public_externs" ] || {
    printf '%s\n' "$public_externs" >&2
    fail "public headers must not expose mutable global state"
}

for facade in \
    src/presentation/render.c \
    src/space/space.c \
    src/world/chunks.c
do
    lines=$(wc -l < "$facade")
    [ "$lines" -le 10 ] || fail "$facade must remain a tiny facade"
done

for split_file in \
    src/presentation/render_scene.c \
    src/presentation/render_planets.c \
    src/presentation/render_sky.c \
    src/presentation/render_ui.c \
    src/space/space_state.c \
    src/space/space_runtime.c \
    src/space/space_query.c \
    src/space/planet_world.c \
    src/space/planet_lighting.c \
    src/world/chunks_storage.c \
    src/world/chunks_streaming.c \
    src/world/chunks_mesh.c \
    src/world/chunks_water.c \
    src/world/chunks_runtime.c \
    src/ecology/entity.c \
    src/ecology/entity_simulation.c \
    src/app/game.c \
    src/app/game_landing.c \
    src/app/game_biology.c \
    src/app/game_capture.c
do
    [ -f "$split_file" ] || fail "missing split implementation $split_file"
    lines=$(wc -l < "$split_file")
    [ "$lines" -le 1500 ] || fail "$split_file exceeds the 1500-line hotspot limit"
done

ship_lines=$(wc -l < src/gameplay/ship.c)
[ "$ship_lines" -le 1800 ] ||
    fail "src/gameplay/ship.c exceeds its 1800-line responsibility limit"
ship_visual_lines=$(wc -l < src/gameplay/ship_visual.c)
[ "$ship_visual_lines" -le 700 ] ||
    fail "src/gameplay/ship_visual.c exceeds its 700-line responsibility limit"

oversized_files=$(
    find src -name '*.c' -exec wc -l {} \; |
        awk '$1 > 2100 { print $2 " (" $1 " lines)" }'
)
[ -z "$oversized_files" ] || {
    printf '%s\n' "$oversized_files" >&2
    fail "production source exceeds the global 2100-line limit"
}

grep -q '^void DrawWorld(' src/presentation/render_scene.c ||
    fail "world rendering must live in render_scene.c"
grep -q '^void EntitiesUpdate(' src/ecology/entity_simulation.c ||
    fail "entity simulation must live in entity_simulation.c"
grep -q '^void UpdateChunks(' src/world/chunks_streaming.c ||
    fail "chunk streaming must live in chunks_streaming.c"
grep -q '^bool PlanetWorldLandingTarget(' src/space/planet_world.c ||
    fail "planet transitions must live in planet_world.c"

grep -q '^include mk/modules.mk' Makefile ||
    fail "Makefile must source the module manifest"
grep -q -- '-MMD -MP' Makefile ||
    fail "production compilation must emit header dependencies"
grep -q '$(OBJ_DIR)/%.o: src/%.c' Makefile ||
    fail "production sources must compile to one object per source"
grep -q 'cp $(TARGET) README.md' Makefile ||
    fail "release packaging must use the selected build target"

if grep -n '#[[:space:]]*include[[:space:]]*"fluid.h"' \
    src/world/world.c src/world/chunks.c; then
    fail "world and chunks must use extension hooks instead of fluid includes"
fi

if grep -R -n '^TerrainMode terrainMode[[:space:]]*=' src --include='*.c'; then
    fail "terrain mode ownership must remain inside world.c"
fi

grep -q 'GameUpdateInteractions(' src/app/game.c ||
    fail "game frame updates must delegate world interactions"
if grep -nE 'RaycastBlocksFiltered|FluidTryCollectUnit|FluidTryDepositUnit|ShipTryEnter' \
    src/app/game.c; then
    fail "world interaction mechanics must remain in game_interaction.c"
fi

grep -q 'GameDispatchDebugCommand(&game)' src/app/game.c ||
    fail "game loop must delegate debug command dispatch"
if grep -n 'DebugControlPoll' src/app/game.c; then
    fail "debug command routing must remain in game_debug.c"
fi

render_frame_body=$(sed -n '/^static void GameRenderFrame(/,/^}/p' src/app/game.c)
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
