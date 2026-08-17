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

if grep -R -n '#include "app/' \
    src/core src/world src/space src/ecology src/gameplay src/presentation \
    --include='*.c' --include='*.h'; then
    fail "only app may depend on app headers"
fi

if grep -R -nE '#include "(world|space|ecology|gameplay|presentation)/' \
    src/core --include='*.c' --include='*.h'; then
    fail "core must not depend on higher-level modules"
fi

domain_effect_calls=$(
    grep -R -nE \
        '(AudioPlay(Break|Step|WaterStep|Splash)|AudioSetRain|ParticlesEmit(One|Burst|Styled))[[:space:]]*\(' \
        src/world src/space src/ecology src/gameplay \
        --include='*.c' --include='*.h' || true
)
[ -z "$domain_effect_calls" ] || {
    printf '%s\n' "$domain_effect_calls" >&2
    fail "domain modules must publish neutral effects instead of calling presentation"
}

effect_dispatch_count=$(grep -c 'EffectDispatchPending();' src/app/game.c || true)
[ "$effect_dispatch_count" -ge 2 ] ||
    fail "app must dispatch domain effects at both frame boundaries"

for public_header in \
    src/world/chunks.h \
    src/world/nether.h \
    src/space/space.h
do
    if grep -n '#include "presentation/' "$public_header"; then
        fail "$public_header must expose neutral metrics, not presentation types"
    fi
done

space_facade_imports=$(
    grep -R -n '#include "space/space.h"' src \
        --include='*.c' --include='*.h' |
        grep -v '^src/space/space.c:' || true
)
[ -z "$space_facade_imports" ] || {
    printf '%s\n' "$space_facade_imports" >&2
    fail "production code must use narrow space interfaces"
}
for space_header in \
    src/space/space_types.h \
    src/space/space_runtime.h \
    src/space/space_state.h \
    src/space/space_query.h \
    src/space/space_chunks.h \
    src/space/space_world_transition.h \
    src/space/space_persistence.h
do
    [ -f "$space_header" ] || fail "missing narrow space interface $space_header"
done
if grep -nE '#include "(world|gameplay|presentation)/' \
    src/space/space_types.h; then
    fail "space value types must not depend on higher-level modules"
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
    src/presentation/album_ui.c \
    src/presentation/scanner_overlay.c \
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
    src/app/game_world_transition.c \
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
    fail "space must own neutral planet landing target selection"

grep -q '^include mk/modules.mk' Makefile ||
    fail "Makefile must source the module manifest"
grep -q -- '-MMD -MP' Makefile ||
    fail "production compilation must emit header dependencies"
grep -q '$(OBJ_DIR)/%.o: src/%.c' Makefile ||
    fail "production sources must compile to one object per source"
grep -q '^TEST_HEADERS :=' Makefile ||
    fail "test builds must track project headers"
grep -q '\$(TEST_TARGETS) \$(CHUNK_BENCHMARK_TARGET): \$(TEST_HEADERS)' Makefile ||
    fail "test binaries must rebuild after transitive header changes"
grep -q 'cp $(TARGET) README.md' Makefile ||
    fail "release packaging must use the selected build target"

grep -q '^GraphicsQualityProfile GraphicsQualityProfileFor(' \
    src/presentation/render_quality.c ||
    fail "presentation must own graphics quality profiles"
if grep -n 'GraphicsQualityProfileFor' src/app/game_settings.c; then
    fail "settings persistence must not own rendering quality policy"
fi

grep -q '^void GameSaveMap(' src/app/game_save.c ||
    fail "app must coordinate cross-module persistence"
grep -q '^void GameLoadMap(' src/app/game_save.c ||
    fail "app must coordinate cross-module restore"
if grep -nE '\b(Inventory|Ship|PlanetWorld|HomeWorld|Album|Space|Entities|PlanetEcology|EvolutionCatalog|ShipLocator|MapMarkers)(Save|Load|ReadState|SaveState)' \
    src/world/world.c; then
    fail "world must not coordinate other modules' persistence"
fi
if grep -n 'gameplay/player_types.h' src/world/world.h; then
    fail "world API must not depend on player state"
fi

grep -q '^bool GameWorldTransitionBeginDescent(' \
    src/app/game_world_transition.c ||
    fail "app must coordinate surface entry"
grep -q '^bool GameWorldTransitionTryLaunch(' \
    src/app/game_world_transition.c ||
    fail "app must coordinate surface launch"
if grep -nE '\b(Player|DrainChunkGen|UnloadAll(Space)?Chunks|UpdateChunks|RebuildTorchList|ClearUndoHistory|WorldSetNetherActive|SetImportMessage|FindSafeSurfaceLanding|TerrainHeight|PlanetTerrainHeight|PlanetEcology)' \
    src/space/planet_world.c; then
    fail "space planet state must not coordinate player, world, or ecology"
fi
if grep -nE '\b(HomeWorldTryEnter|PlanetWorldTryEnter)\b' \
    src/gameplay/ship.c; then
    fail "ship exit must not trigger a world transition"
fi

if grep -nE '\b(Draw|Texture2D|LoadTexture|UnloadTexture|GetScreen|GetMouse|IsMouse|UiDrawText)' \
    src/gameplay/album.c src/gameplay/album.h \
    src/gameplay/discovery.c src/gameplay/discovery.h; then
    fail "gameplay album and discovery must expose data, not presentation"
fi
if grep -n '#include "presentation/' \
    src/gameplay/album.c src/gameplay/album.h \
    src/gameplay/discovery.c src/gameplay/discovery.h; then
    fail "gameplay album and discovery must not depend on presentation"
fi
grep -q '^void AlbumUiDraw(' src/presentation/album_ui.c ||
    fail "presentation must own album drawing"
grep -q '^void DrawPlanetPoiScanner(' src/presentation/scanner_overlay.c ||
    fail "presentation must own the planet scanner overlay"

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
