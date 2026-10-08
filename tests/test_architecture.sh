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

[ -f src/core/plugin.h ] || fail "core plugin ABI header is missing"
[ -f src/core/plugin.c ] || fail "core plugin host implementation is missing"
[ -f mk/plugins.mk ] || fail "plugin build manifest is missing"
[ -f plugins/manifest.mk ] || fail "plugin source manifest is missing"
grep -q '^plugins:' Makefile || fail "Makefile must expose the plugins target"
grep -q 'VoxelcraftPluginGetDescriptor' src/core/plugin.c ||
    fail "plugin host must resolve the descriptor entry point"

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

# Cross-domain rules intentionally target public/narrow interfaces and direct
# bypasses only. Terrain/entity implementations may still consume narrow
# contracts from another domain; a full include ban would reject valid seams.
for world_header in \
    src/world/world_types.h \
    src/world/world_environment.h \
    src/world/world_extension.h \
    src/world/world_persistence.h \
    src/world/chunks.h \
    src/world/nether.h \
    src/world/weather_model.h \
    src/world/weather_impact.h
do
    if grep -nE '#include "(space|ecology)/' "$world_header"; then
        fail "$world_header must expose neutral world contracts"
    fi
done

for ecology_header in src/ecology/*.h
do
    if grep -n '#include "space/' "$ecology_header"; then
        fail "$ecology_header must not depend on space headers"
    fi
done

space_direct_ecology_imports=$(
    grep -R -n '#include "ecology/' src/space \
        --include='*.c' --include='*.h' |
        grep -v 'src/space/space_dependencies.h:' || true
)
[ -z "$space_direct_ecology_imports" ] || {
    printf '%s\n' "$space_direct_ecology_imports" >&2
    fail "space must consume ecology only through its dependency aggregator"
}

if grep -R -nE '#include "(space/space\.h|space/space_dependencies\.h|ecology/ecology_internal\.h)"' \
    src/world --include='*.c' --include='*.h'; then
    fail "world must use narrow domain contracts, not aggregate/private facades"
fi

if grep -R -n '#include "space/space_state.h"' src/world \
    --include='*.c' --include='*.h'; then
    fail "world surface state must enter through WorldSurfaceSnapshot"
fi

world_space_state_getters=$(
    grep -R -nE \
        '\b(SpaceElapsedSimulationTime|HomeWorldSurfaceIsActive|PlanetWorld(IsActive|Seed|Style|Profile|GravityScale|IsDarkSide|OriginX|OriginZ))[[:space:]]*\(' \
        src/world --include='*.c' --include='*.h' || true
)
[ -z "$world_space_state_getters" ] || {
    printf '%s\n' "$world_space_state_getters" >&2
    fail "world must use WorldSurfaceSnapshot and receive simulation time explicitly"
}

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

if grep -nE '\b(SetImportMessage|WorldGetImportMessage|WorldTickImportMessage)\b' \
    src/world/world.c src/world/world.h; then
    fail "world must not own cross-module status notices"
fi
grep -q '^void GameNoticePost(' src/core/game_notice.c ||
    fail "core must own neutral status notices"

effect_dispatch_count=$(
    grep -h 'EffectDispatchPending();' \
        src/app/game.c src/app/game_environment.c | wc -l
)
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
    src/presentation/render_debug_hud.c \
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
    src/world/chunks_runtime_diagnostics.c \
    src/world/terrain_structures.c \
    src/ecology/entity.c \
    src/ecology/entity_simulation.c \
    src/ecology/ecology_population_environment.c \
    src/gameplay/ship_runtime.c \
    src/app/game.c \
    src/app/game_hud.c \
    src/app/game_streaming.c \
    src/app/game_landing.c \
    src/app/game_world_transition.c \
    src/app/game_biology.c \
    src/app/game_capture.c \
    src/app/game_debug_weather.c \
    src/app/game_debug_diagnostics.c \
    src/app/game_hydrology.c \
    src/app/game_perf_route.c \
    src/app/game_plugins.c
do
    [ -f "$split_file" ] || fail "missing split implementation $split_file"
    lines=$(wc -l < "$split_file")
    [ "$lines" -le 1500 ] || fail "$split_file exceeds the 1500-line hotspot limit"
done

ship_lines=$(wc -l < src/gameplay/ship.c)
[ "$ship_lines" -le 1750 ] ||
    fail "src/gameplay/ship.c exceeds its 1750-line responsibility limit"
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
grep -q '^int GameUpdateWorldStreaming(' src/app/game_streaming.c ||
    fail "world streaming update must live in game_streaming.c"
grep -q '^void GameApplyPerfRoute(' src/app/game_perf_route.c ||
    fail "performance route must live in game_perf_route.c"
grep -q '^bool GamePluginsStart(' src/app/game_plugins.c ||
    fail "plugin lifecycle must live in game_plugins.c"
grep -q '^ChunkPreloadInfo ChunksGetPreloadInfo(' \
    src/world/chunks_runtime_diagnostics.c ||
    fail "chunk preload diagnostics must live in chunks_runtime_diagnostics.c"
grep -q '^bool ChunksGetSectionPipelineInfo(' \
    src/world/chunks_runtime_diagnostics.c ||
    fail "chunk pipeline diagnostics must live in chunks_runtime_diagnostics.c"
grep -q '^typedef struct ChunkMeshBuildRequest {' \
    src/world/chunks_internal.h ||
    fail "chunk mesh builders must use a named request"
grep -q '^bool BuildChunkMeshData(' src/world/chunks_mesh.c ||
    fail "chunk mesh implementation must accept the named request"
if grep -n '^bool BuildMeshDataFilteredWithSnapshot' \
    src/world/chunks_internal.h src/world/chunks_mesh.c; then
    fail "chunk mesh internals must not restore positional filtered builders"
fi
grep -q '^bool PlanetWorldLandingTarget(' src/space/planet_world.c ||
    fail "space must own neutral planet landing target selection"

grep -q '^include mk/modules.mk' Makefile ||
    fail "Makefile must source the module manifest"
grep -q '^include mk/components.mk' Makefile ||
    fail "Makefile must source the component manifest"
grep -q '^COMPONENT_NAMES := core world space ecology gameplay presentation app' \
    mk/components.mk ||
    fail "component manifest must enumerate all ownership domains"
test ! -e src/presentation/render_resources.h ||
    fail "resource accounting contract must not remain in presentation"
test ! -e src/presentation/render_resources.c ||
    fail "resource accounting implementation must not remain in presentation"
grep -q '^typedef struct RenderResourceSnapshot' src/core/render_resources.h ||
    fail "core must own the shared resource accounting snapshot"
grep -q '^uint64_t RenderMeshEstimatedBytes' src/core/render_resources.c ||
    fail "core must own resource accounting implementation"
if grep -R -n 'presentation/render_resources.h' src; then
    fail "simulation components must not include presentation resource headers"
fi
grep -q '^typedef struct WorldLightingState' src/world/world_lighting.h ||
    fail "world must own the neutral physical lighting state"
if grep -nE '#include "presentation/|EnvironmentPresentation|WorldLightingCompose|WorldLightingFallbackPresentation|WorldLightingUnderwaterFogColor' \
    src/world/world_lighting.h; then
    fail "world lighting contract must not depend on presentation"
fi
test ! -e src/world/world_lighting.c ||
    fail "world lighting composition must live in presentation"
grep -q '^WorldLightingState WorldLightingCompose' \
    src/presentation/world_lighting.c ||
    fail "presentation must own physical/presentation lighting composition"
grep -q '^EnvironmentPresentationState WorldLightingFallbackPresentation' \
    src/presentation/world_lighting.c ||
    fail "presentation must own fallback environment composition"
if grep -nE '#include "presentation/|WeatherVisual(State|Input)|WeatherVisualStateEvaluate' \
    src/world/weather.h src/world/weather.c; then
    fail "world weather must expose physical samples without presentation types"
fi
grep -q '^bool WeatherFieldSampleInterpolatedAtWorldTime' \
    src/world/weather.c ||
    fail "world must own interpolated physical weather sampling"
grep -q '^WeatherVisualState WeatherVisualStateAtWorld' \
    src/presentation/weather_presentation.c ||
    fail "presentation must own world weather visual composition"
if grep -R -n '#include "ecology/ecology.h"' src/world; then
    fail "world must consume ecology runtime through neutral hooks"
fi
if grep -R -nE 'PlanetEcology(LocalAt|FloraRuntime|StaticSuitabilityAt|ApplyToChunk)' \
    src/world; then
    fail "world must not call ecology runtime services directly"
fi
grep -q '^typedef struct WorldEcologyHooks' src/world/ecology_hooks.h ||
    fail "world must expose a neutral ecology hook contract"
grep -q '^void GameInstallWorldEcologyHooks' src/app/game_biology.c ||
    fail "app must compose world and ecology runtime services"
if grep -n '#include "presentation/' src/app/game_biology.c \
    src/app/game_biology.h; then
    fail "app biology hooks must not own presentation UI"
fi
grep -q '^void BiologyAtlasDraw' src/presentation/biology_ui.c ||
    fail "biology atlas UI must live in presentation"
grep -q '^void DrawEvolutionScanPanel' src/presentation/biology_ui.c ||
    fail "evolution scan UI must live in presentation"
grep -q '^include mk/tests.mk' Makefile ||
    fail "Makefile must source the declarative test manifest"
grep -q '^define define_test' mk/tests.mk ||
    fail "test manifest must provide the shared declaration template"
manifest_test_count=$(grep -c '^\$(eval \$(call define_test,' mk/tests.mk)
test_source_count=$(find tests -maxdepth 1 -name 'test_*.c' -print | wc -l)
[ "$manifest_test_count" -eq "$test_source_count" ] ||
    fail "test manifest count does not match test source count"
test_target_count=$(make -s -pn | awk '/^TEST_TARGETS :=/ { print NF - 2 }')
[ "$test_target_count" -eq "$test_source_count" ] ||
    fail "TEST_TARGETS count does not match test source count"
grep -q -- '-MMD -MP' Makefile ||
    fail "production compilation must emit header dependencies"
grep -q '$(OBJ_DIR)/%.o: src/%.c' Makefile ||
    fail "production sources must compile to one object per source"
grep -q '^TEST_HEADERS :=' Makefile ||
    fail "test builds must track project headers"
grep -Fq '$(TEST_BUILD_DIR)/%: tests/%.c' Makefile ||
    fail "test builds must use the shared pattern rule"
grep -Fq '$(TEST_HEADERS)' Makefile ||
    fail "test binaries must rebuild after transitive header changes"
grep -q '^BUILD_CONFIG := .*\.build-config' Makefile ||
    fail "build must define a configuration fingerprint"
grep -q '^\$(BUILD_CONFIG): FORCE' Makefile ||
    fail "build configuration fingerprint must be content-stable"
if grep -nE '^\s*\$\(CC\).*tests/test_' Makefile; then
    fail "test binaries must not use per-target hand-written recipes"
fi
if grep -nF '$(AR) rcs $@ $^' Makefile; then
    fail "module archives must not package non-object build metadata"
fi
grep -q '^PUBLIC_HEADERS :=' Makefile ||
    fail "build must identify independently compilable public headers"
grep -q '^test: test-headers test-modules ' Makefile ||
    fail "test must enforce public header and module build gates"
grep -q '^test-modules: \$(MODULE_ARCHIVES)' Makefile ||
    fail "module build gate must compile every module archive"
grep -q '^\$(TARGET): \$(MODULE_ARCHIVES)' Makefile ||
    fail "executable must be composed from component archives"
grep -q -- '-Wl,--start-group \$(MODULE_ARCHIVES) -Wl,--end-group' Makefile ||
    fail "executable link must resolve component dependencies as a group"
for component in core world space ecology gameplay presentation app
do
    grep -q "define_component,$component," mk/components.mk ||
        fail "missing independent component target: $component"
done
grep -q "sh scripts/clean-build.sh '.*BUILD_ROOT.*'" Makefile ||
    fail "clean must validate BUILD_ROOT before recursive deletion"
grep -q "BUILD_VARIANT=ci CFLAGS='\$(CI_CFLAGS)' test" Makefile ||
    fail "CI warning checks must use an isolated build variant"
if grep -R -n '#include ".*_internal\.h"' src \
    --include='*.h' | grep -v '_internal\.h:'; then
    fail "public headers must not include private module headers"
fi
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
grep -q '^bool GameLoadMap(' src/app/game_save.c ||
    fail "app must coordinate cross-module restore"
if grep -nE '\b(Inventory|Ship|PlanetWorld|HomeWorld|Album|Space|Entities|PlanetEcology|EvolutionCatalog|ShipLocator|MapMarkers)(Save|Load|ReadState|SaveState)' \
    src/world/world.c; then
    fail "world must not coordinate other modules' persistence"
fi
if grep -n 'gameplay/player_types.h' src/world/world.h; then
    fail "world API must not depend on player state"
fi
if grep -nE '#include "gameplay/ship\.h"|\bShip(BlockIsParkedCore|TrackParkedAt|ForgetParkedAt)\b' \
    src/world/world.c; then
    fail "world block commits must publish a neutral extension event"
fi
if grep -R -nE '#include "gameplay/|\bPlanetPoiApplyToChunk\b' \
    src/world --include='*.c' --include='*.h'; then
    fail "world terrain must use an app-installed neutral decorator"
fi
grep -q '^static void GameOnWorldBlockCommitted(' src/app/game.c ||
    fail "app must coordinate parked ship tracking after world block commits"
grep -q '\.onBlockCommitted = GameOnWorldBlockCommitted' src/app/game.c ||
    fail "app must install the parked ship block-commit handler"

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
grep -q 'GameDebugWeatherDslResolve(game, name, outValue)' \
    src/app/game_debug.c ||
    fail "game debug DSL must delegate weather values"
grep -q '^bool GameDebugWeatherDslResolve(' \
    src/app/game_debug_weather.c ||
    fail "weather debug module must own weather DSL values"
grep -q 'GameDebugHydrologyDslResolve(game, name, outValue)' \
    src/app/game_debug.c ||
    fail "game debug DSL must delegate hydrology values"
grep -q '^bool GameDebugHydrologyDslResolve(' \
    src/app/game_debug_hydrology.c ||
    fail "hydrology debug module must own hydrology DSL values"

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

sh tests/test_clean_build.sh

echo "architecture checks passed"
