#include "raylib.h"
#include "raymath.h"

#include "app/game.h"
#include "app/game_biology.h"
#include "app/game_debug.h"
#include "app/game_debug_trace.h"
#include "app/game_interaction.h"
#include "app/game_internal.h"
#include "app/game_landing.h"
#include "app/game_runtime.h"
#include "app/game_save.h"
#include "app/game_world_transition.h"
#include "core/game_effects.h"
#include "core/game_notice.h"
#include "world/world_types.h"
#include "world/terrain.h"
#include "world/world.h"
#include "world/world_extension.h"
#include "world/block_atlas.h"
#include "world/chunks.h"
#include "gameplay/player.h"
#include "gameplay/interaction.h"
#include "gameplay/album.h"
#include "gameplay/discovery.h"
#include "gameplay/inventory.h"
#include "gameplay/map_markers.h"
#include "presentation/render.h"
#include "presentation/render_resources.h"
#include "presentation/render_ui.h"
#include "presentation/album_ui.h"
#include "presentation/scanner_overlay.h"
#include "presentation/particles.h"
#include "presentation/audio.h"
#include "world/weather.h"
#include "space/space_chunks.h"
#include "space/space_query.h"
#include "space/space_runtime.h"
#include "space/space_state.h"
#include "space/space_units.h"
#include "world/world_environment.h"
#include "gameplay/ship.h"
#include "world/nether.h"
#include "ecology/entity.h"
#include "presentation/entity_renderer.h"
#include "world/fluid.h"
#include "ecology/evolution_catalog.h"
#include "presentation/starmap.h"
#include "presentation/homeworld_map.h"
#include "ecology/ecology.h"
#include "core/perf.h"
#include "presentation/world_renderer.h"
#include "world/world_lighting.h"
#include "presentation/environment_presentation.h"
#include "presentation/environment_runtime.h"
#include "presentation/effect_dispatch.h"
#include "app/game_settings.h"
#include "app/screenshot.h"
#include "core/debug_control.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool GameWorldSimulationPaused(const GameRuntime *game)
{
    return game && (game->paused || HomeWorldMapIsOpen());
}

static void GameOnWorldBlockCommitted(
    int x, int y, int z, BlockType previous, BlockType next)
{
    bool previousIsShip = previous == BLOCK_SPACESHIP ||
                          ShipBlockIsParkedCore(previous);
    bool nextIsShip = next == BLOCK_SPACESHIP || ShipBlockIsParkedCore(next);
    if (previousIsShip && !nextIsShip) {
        ShipForgetParkedAt(x, y, z);
    } else if (!previousIsShip && nextIsShip) {
        ShipTrackParkedAt(x, y, z);
    }
}

static bool NewWorldSpawnCandidate(int x, int z, TerrainMode mode,
                                   Vector3 *outPosition)
{
    int height = TerrainHeight(x, z, mode);
    int seaLevel = TerrainSeaLevel(mode);
    if ((seaLevel >= 0 && height < seaLevel) || ShouldPlaceTree(x, z, mode)) {
        return false;
    }
    if (height < SURFACE_MIN_Y ||
        height + 3 >= SURFACE_MAX_Y_EXCLUSIVE) return false;
    if (outPosition) {
        *outPosition = (Vector3){ (float)x + 0.5f, (float)height + 1.01f,
                                  (float)z + 0.5f };
    }
    return true;
}

static Vector3 FindNewWorldSpawn(TerrainMode mode)
{
    const int searchRadius = 512;
    Vector3 candidate = { 0 };
    for (int radius = 0; radius <= searchRadius; radius++) {
        if (radius == 0) {
            if (NewWorldSpawnCandidate(0, 0, mode, &candidate)) return candidate;
            continue;
        }
        for (int offset = -radius; offset <= radius; offset++) {
            if (NewWorldSpawnCandidate(offset, -radius, mode, &candidate) ||
                NewWorldSpawnCandidate(radius, offset, mode, &candidate) ||
                NewWorldSpawnCandidate(-offset, radius, mode, &candidate) ||
                NewWorldSpawnCandidate(-radius, -offset, mode, &candidate)) {
                return candidate;
            }
        }
    }

    int seaLevel = TerrainSeaLevel(mode);
    int platformY = seaLevel >= 0 ? seaLevel + 1 : TerrainHeight(0, 0, mode) + 1;
    for (int z = -1; z <= 1; z++) {
        for (int x = -1; x <= 1; x++) {
            SetBlockNoUndo(x, platformY, z, BLOCK_PLANK);
            SetBlockNoUndo(x, platformY + 1, z, BLOCK_AIR);
            SetBlockNoUndo(x, platformY + 2, z, BLOCK_AIR);
        }
    }
    return (Vector3){ 0.5f, (float)platformY + 1.01f, 0.5f };
}

static void ApplyPerfRoute(Player *player, int frame)
{
    const float dt = 1.0f / 60.0f;
    Vector3 previous = player->position;
    float x = 0.5f;
    float z = 0.5f;
    if (frame >= 120 && !PerfRouteComplete()) {
        int phase = (frame - 120) % 300;
        if (phase < 150) {
            x += (float)phase * 0.78f;
            z += (float)phase * 0.39f;
        } else {
            int returning = phase - 150;
            x += 117.0f - (float)returning * 0.78f;
            z += 58.5f - (float)returning * 0.39f;
        }
    }
    player->position = (Vector3){ x,
        (float)TerrainHeight((int)floorf(x), (int)floorf(z),
                             WorldTerrainMode()) + 3.0f, z };
    Vector3 delta = Vector3Scale(Vector3Subtract(player->position, previous), 1.0f / dt);
    player->velocity = delta;
    if (Vector3LengthSqr(delta) > 0.001f) player->yaw = atan2f(delta.x, delta.z);
    player->pitch = -0.18f;
    player->onGround = true;
    player->floating = false;
}

static RenderResourceSnapshot CurrentRenderResourceSnapshot(void)
{
    RenderResourceSnapshot snapshot = ChunksGetRenderResourceSnapshot();
    RenderResourceSnapshotMerge(&snapshot, SpaceGetRenderResourceSnapshot());
    RenderResourceSnapshotMerge(&snapshot, NetherGetRenderResourceSnapshot());
    snapshot.worldLightingTextureBytes = WorldRendererTextureBytes();
    return snapshot;
}
static void BeginNewWorld(GameRuntime *game, TerrainMode mode, uint32_t seed)
{
    DrainChunkGen();
    UnloadAllChunks();
    SpaceReset();
    NetherReset();
    AlbumReset();
    AlbumUiReset();
    WorldReset(seed);
    MapMarkersReset();
    EvolutionCatalogReset();
    PlanetEcologyResetState();
    InventoryReset();
    InventoryGrantStarterKit();
    ShipReset();
    ShipLocatorReset();
    StarMapClose();
    EntitiesClear();
    ParticlesClear();
    GameEffectsReset();
    WeatherInit();

    WorldSetTerrainMode(mode);
    game->player.position = FindNewWorldSpawn(WorldTerrainMode());
    game->player.velocity = Vector3Zero();
    game->player.yaw = PI;
    game->player.pitch = -0.25f;
    game->player.onGround = false;
    game->player.floating = false;
    PlayerResetRuntimeState(&game->player);

    game->autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
    game->dayTime = 0.30f;
    game->dayCycleEnabled = true;
    UpdateChunks(game->player.position,
                 EffectiveRenderDistanceForHeight(
                     game->player.position.y + EYE_HEIGHT));
}

static bool GameUpdateStartScreen(GameRuntime *game, float dt,
                                  bool debugStartRequested)
{
    if (game->perfMode || game->screen != SCREEN_START) return false;

    AudioSetEnvironment(NULL);
    AudioUpdate(dt);
    bool startGame = false;
    if (IsKeyPressed(KEY_ESCAPE)) game->quitRequested = true;
    BeginDrawing();
    DrawStartPage(&startGame, &game->quitRequested, &game->selectedTerrain,
                  &game->selectedSeed);
    if (debugStartRequested) startGame = true;
    EndDrawing();

    if (startGame) {
        BeginNewWorld(game, game->selectedTerrain, game->selectedSeed);
        EnvironmentPresentationRuntimeReset(&game->environment);
        game->importDialog.open = false;
        game->importDialog.relief = true;
        game->importDialog.maxBlocks = IMPORT_DEFAULT_BLOCKS;
        game->importDialog.path[0] = '\0';
        game->albumOpen = false;
        game->albumRainSuspended = false;
        game->wasInSpace = false;
        game->entitiesWorldActive = true;
        game->entitiesWorldDimension = 0u;
        game->thirdPerson = false;
        game->shipLocatorEnabled = false;
        game->landingTransition = (LandingTransition){ 0 };
        game->paused = false;
        game->screen = SCREEN_PLAYING;
        game->cursorReleased = false;
        DisableCursor();
        GameNoticePost(
            WorldTerrainMode() == TERRAIN_FLAT
                ? TextFormat("Flat world seed %u. Press I to import.",
                             WorldGetSeed())
                : TextFormat("World seed %u.", WorldGetSeed()));
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL start ok seed=%u\n",
                          WorldGetSeed());
    }
    return true;
}

static void GameUpdateAlbum(GameRuntime *game)
{
    if (!game->albumOpen && !game->importDialog.open && !game->paused &&
        !HomeWorldMapIsOpen() && !game->landingTransition.active &&
        IsKeyPressed(KEY_P)) {
        if (WeatherGetCurrent() == WEATHER_RAIN) {
            game->albumRainSuspended = true;
            AudioSetRain(false);
        }
        AlbumUiOpen();
        game->albumOpen = true;
        game->player.velocity = Vector3Zero();
        game->cursorReleased = false;
        EnableCursor();
    }

    AlbumUiEvent albumEvent = AlbumUiUpdate();
    switch (albumEvent) {
    case ALBUM_UI_EVENT_IMAGE_ADDED:
        GameNoticePost("Added image to album.");
        break;
    case ALBUM_UI_EVENT_ALBUM_FULL:
        GameNoticePost("Album is full (64 images).");
        break;
    case ALBUM_UI_EVENT_DUPLICATE_IMAGE:
        GameNoticePost("Image already in album.");
        break;
    case ALBUM_UI_EVENT_INVALID_IMAGE:
        GameNoticePost("Unable to add the selected image.");
        break;
    case ALBUM_UI_EVENT_NONE:
    default:
        break;
    }
    if (AlbumUiConsumePlaceRequest()) {
        const char *placedPath = AlbumUiSelectedPath();
        AlbumUiClose();
        game->albumOpen = false;
        if (game->albumRainSuspended) {
            game->albumRainSuspended = false;
            AudioSetRain(true);
        }
        if (!game->paused && !game->cursorReleased &&
            !game->importDialog.open) {
            DisableCursor();
        }
        if (placedPath) {
            ImportImageAsBlocks(placedPath, &game->player,
                                IMPORT_DEFAULT_BLOCKS, false);
        }
    }
    if (!AlbumUiIsOpen() && game->albumOpen) {
        game->albumOpen = false;
        if (game->albumRainSuspended) {
            game->albumRainSuspended = false;
            AudioSetRain(true);
        }
        if (!game->paused && !game->cursorReleased &&
            !game->importDialog.open) {
            DisableCursor();
        }
    }
}

static void GameUpdatePauseAndBiologyAtlas(GameRuntime *game, float dt,
                                           bool landingSkipPressed)
{
    bool biologyAtlasClosed = false;
    if (game->biologyAtlasOpen && IsKeyPressed(KEY_ESCAPE)) {
        game->biologyAtlasOpen = false;
        biologyAtlasClosed = true;
        game->cursorReleased = false;
        DisableCursor();
    }
    if (!game->importDialog.open && !game->albumOpen && !StarMapIsOpen() &&
        !HomeWorldMapIsOpen() &&
        !game->biologyAtlasOpen && !biologyAtlasClosed) {
        if (!game->paused && !game->landingTransition.active &&
            !landingSkipPressed && IsKeyPressed(KEY_ESCAPE)) {
            game->paused = true;
            game->player.velocity = Vector3Zero();
            game->cursorReleased = false;
            EnableCursor();
        } else if (game->paused &&
                   (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER))) {
            game->paused = false;
            DisableCursor();
        }
    }

    if (!game->paused && !game->albumOpen && !game->importDialog.open &&
        !game->landingTransition.active && !game->biologyAtlasOpen &&
        !HomeWorldMapIsOpen()) {
        SpaceAdvanceTime(dt);
    }

    if (game->biologyAtlasOpen && !biologyAtlasClosed &&
        IsKeyPressed(KEY_B)) {
        game->biologyAtlasOpen = false;
        game->cursorReleased = false;
        DisableCursor();
    } else if (!game->biologyAtlasOpen && !game->importDialog.open &&
               !game->paused && !game->albumOpen &&
               !game->landingTransition.active && !game->cursorReleased &&
               IsKeyPressed(KEY_B)) {
        game->biologyAtlasOpen = true;
        game->biologyAtlasSlot = EvolutionCatalogFirstSpeciesSlot();
        game->player.velocity = Vector3Zero();
        game->cursorReleased = true;
        EnableCursor();
    }
    if (game->biologyAtlasOpen) {
        if (IsKeyPressed(KEY_UP)) {
            game->biologyAtlasSlot =
                BiologyAtlasNextSlot(game->biologyAtlasSlot, -1);
        } else if (IsKeyPressed(KEY_DOWN)) {
            game->biologyAtlasSlot =
                BiologyAtlasNextSlot(game->biologyAtlasSlot, 1);
        }
    }
}

static float GameSurfaceMapDaylight(const GameRuntime *game)
{
    if (WorldCurrentDimension() == WORLD_DIMENSION_PLANET) {
        return PlanetWorldDaylightAt(game->player.position);
    }
    float daylight = 1.0f;
    float sunset = 0.0f;
    DayNightFactors(game->dayTime, &daylight, &sunset);
    return daylight;
}

static void GameUpdateViewModes(GameRuntime *game)
{
    GameUpdateAlbum(game);

    if (!game->importDialog.open && !game->paused && !game->albumOpen &&
        !HomeWorldMapIsOpen() &&
        !game->landingTransition.active && IsKeyPressed(KEY_TAB)) {
        game->cursorReleased = !game->cursorReleased;
        if (game->cursorReleased) {
            game->player.velocity = Vector3Zero();
            EnableCursor();
        } else {
            DisableCursor();
        }
    }
    if (!game->landingTransition.active && !HomeWorldMapIsOpen() &&
        ShipIsDriving() &&
        IsKeyPressed(KEY_E)) {
        if (!WorldIsSpaceActive() ||
            !LandingTransitionBegin(&game->landingTransition,
                                    &game->player)) {
            ShipExit(&game->player);
        }
    }
    bool openedHomeMap = false;
    bool homeMapWasOpen = HomeWorldMapIsOpen();
    WorldDimension currentDimension = WorldCurrentDimension();
    bool surfaceMapAvailable = currentDimension == WORLD_DIMENSION_HOME ||
        (currentDimension == WORLD_DIMENSION_PLANET && PlanetWorldIsActive());
    if (!game->landingTransition.active && surfaceMapAvailable &&
        IsKeyPressed(KEY_M) && !HomeWorldMapIsOpen() &&
        !StarMapIsOpen() && !game->paused && !game->cursorReleased) {
        HomeWorldMapOpen(game->player.position,
                         GameSurfaceMapDaylight(game));
        game->player.velocity = Vector3Zero();
        game->cursorReleased = true;
        EnableCursor();
        openedHomeMap = true;
    }
    if (!openedHomeMap && HomeWorldMapIsOpen()) {
        HomeWorldMapUpdate(game->player.position, game->player.yaw,
                           GameSurfaceMapDaylight(game));
        if (!HomeWorldMapIsOpen()) {
            game->cursorReleased = false;
            DisableCursor();
        }
    }
    if (!homeMapWasOpen && !game->landingTransition.active &&
        !HomeWorldMapIsOpen() &&
        WorldCurrentDimension() != WORLD_DIMENSION_PLANET &&
        IsKeyPressed(KEY_M) && !StarMapIsOpen() && !game->paused &&
        !game->cursorReleased) {
        StarMapOpen();
        game->player.velocity = Vector3Zero();
        game->cursorReleased = true;
        EnableCursor();
    }
    if (StarMapIsOpen()) {
        SolarSystemDef destination = { 0 };
        StarMapUpdate(game->player.position);
        if (StarMapConsumeTravel(&destination)) {
            ShipBeginSystemWarp(&game->player, destination.anchorX,
                                destination.anchorZ);
            StarMapClose();
            game->cursorReleased = false;
            DisableCursor();
        }
        if (!StarMapIsOpen()) {
            game->cursorReleased = false;
            DisableCursor();
        }
    }
    if (!game->paused && !game->albumOpen && !game->importDialog.open &&
        !HomeWorldMapIsOpen() &&
        !game->landingTransition.active && IsKeyPressed(KEY_F4)) {
        game->thirdPerson = !game->thirdPerson;
        GameNoticePost(game->thirdPerson ? "Third person view."
                                           : "First person view.");
    }

    bool openedImportDialog = false;
    if (!game->importDialog.open && !game->paused &&
        !HomeWorldMapIsOpen() &&
        !game->landingTransition.active && IsKeyPressed(KEY_I)) {
        OpenImportDialog(&game->importDialog);
        if (game->importDialog.open) {
            openedImportDialog = true;
            game->cursorReleased = true;
            game->player.velocity = Vector3Zero();
            EnableCursor();
        }
    }
    if (!openedImportDialog) {
        UpdateImportDialog(&game->importDialog, &game->player,
                           &game->cursorReleased);
    }
}

static bool GameInputBlocked(const GameRuntime *game)
{
    return game->perfMode || game->paused || game->cursorReleased ||
           game->importDialog.open || game->albumOpen ||
           game->biologyAtlasOpen || game->landingTransition.active ||
           ShipIsDriving() || StarMapIsOpen() || HomeWorldMapIsOpen();
}

static void GameUpdateGameplayShortcuts(GameRuntime *game,
                                        bool inputBlocked)
{
    if (!game->paused && !game->albumOpen && !game->importDialog.open &&
        !StarMapIsOpen() && !HomeWorldMapIsOpen() &&
        !game->landingTransition.active &&
        IsKeyPressed(KEY_O)) {
        game->showOrbitTrajectories = !game->showOrbitTrajectories;
        GameNoticePost(game->showOrbitTrajectories
                             ? "Orbit trajectories shown."
                             : "Orbit trajectories hidden.");
    }
    if (!inputBlocked && IsKeyPressed(KEY_F1)) {
        game->showHelp = !game->showHelp;
    }
    if (!inputBlocked && IsKeyPressed(KEY_F3)) {
        game->showDebug = !game->showDebug;
    }
    // Saving while flying is valid; ShipSaveState persists the ship state.
    if (IsKeyPressed(KEY_F5) && !game->paused && !game->cursorReleased &&
        !game->importDialog.open && !game->albumOpen &&
        !game->landingTransition.active && !StarMapIsOpen() &&
        !HomeWorldMapIsOpen()) {
        GameSaveMap(&game->player);
    }
    if (inputBlocked) return;

    int hotbarKey = HotbarKeyToIndex();
    if (hotbarKey >= 0 && hotbarKey < HOTBAR_SIZE) {
        game->selectedIndex = hotbarKey;
    }
    float wheel = GetMouseWheelMove();
    if (wheel > 0.0f) {
        game->selectedIndex =
            (game->selectedIndex + HOTBAR_SIZE - 1) % HOTBAR_SIZE;
    } else if (wheel < 0.0f) {
        game->selectedIndex = (game->selectedIndex + 1) % HOTBAR_SIZE;
    }
    if (IsKeyPressed(KEY_LEFT_BRACKET)) AdjustRenderDistance(-1);
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) AdjustRenderDistance(1);
    if (IsKeyPressed(KEY_F9)) {
        GameLoadMap(&game->player);
        game->landingTransition = (LandingTransition){ 0 };
        game->wasInSpace = WorldIsSpaceActive();
        game->entitiesWorldActive = WorldIsSurfaceActive();
        game->entitiesWorldDimension = WorldCurrentSurfaceId();
        game->cursorReleased = false;
        DisableCursor();
        game->autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
    }
    if (IsKeyPressed(KEY_F6)) {
        game->dayCycleEnabled = !game->dayCycleEnabled;
        GameNoticePost(game->dayCycleEnabled ? "Day/night cycle enabled."
                                               : "Day/night cycle paused.");
    }
    if (IsKeyPressed(KEY_F7)) {
        WeatherCycle();
        GameNoticePost(TextFormat("Weather: %s", WeatherName()));
    }
    if (IsKeyPressed(KEY_F8)) {
        game->autoSaveEnabled = !game->autoSaveEnabled;
        game->autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
        GameNoticePost(game->autoSaveEnabled
                             ? "Auto-save enabled (every 60s)."
                             : "Auto-save disabled.");
    }
    if (IsKeyPressed(KEY_L)) {
        game->shipLocatorEnabled = !game->shipLocatorEnabled;
        if (game->shipLocatorEnabled) {
            GameNoticePost(
                ShipLocatorHasTarget()
                    ? "Ship locator online."
                    : "Ship locator online: deploy or board a ship to "
                      "establish a signal.");
        } else {
            GameNoticePost("Ship locator offline.");
        }
    }
    if (PlanetWorldIsActive() && IsKeyPressed(KEY_C)) {
        game->scannerActive = !game->scannerActive;
        if (game->scannerActive) {
            PlanetPoi poi = { 0 };
            if (PlanetPoiNearest(game->player.position, &poi)) {
                GameNoticePost(
                    TextFormat("Scanner online: %s", poi.name));
            } else {
                GameNoticePost("Scanner online: no signal found.");
            }
        } else {
            GameNoticePost("Scanner offline.");
        }
    }
    bool ctrlHeld =
        IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrlHeld && IsKeyPressed(KEY_Z) && !IsKeyDown(KEY_LEFT_SHIFT)) {
        if (UndoBlockEdit()) GameNoticePost("Undo");
    } else if (ctrlHeld &&
               (IsKeyPressed(KEY_Y) ||
                (IsKeyDown(KEY_LEFT_SHIFT) && IsKeyPressed(KEY_Z)))) {
        if (RedoBlockEdit()) GameNoticePost("Redo");
    }
}

static void GameUpdateTemporalState(GameRuntime *game, float dt)
{
    if (!game->perfMode && game->autoSaveEnabled &&
        game->screen == SCREEN_PLAYING && !GameWorldSimulationPaused(game) &&
        !game->landingTransition.active) {
        game->autoSaveTimer -= dt;
        if (game->autoSaveTimer <= 0.0f) {
            game->autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
            GameSaveMap(&game->player);
        }
    }

    if (!game->perfMode && game->dayCycleEnabled &&
        !GameWorldSimulationPaused(game) &&
        !game->albumOpen && !game->landingTransition.active) {
        game->dayTime += dt / DAY_LENGTH_SECONDS;
        if (game->dayTime >= 1.0f) game->dayTime -= 1.0f;
    }
    if (!GameWorldSimulationPaused(game) && !game->albumOpen &&
        !game->landingTransition.active &&
        (HomeWorldSurfaceIsActive() || PlanetWorldIsActive())) {
        bool weatherSheltered =
            GameEnvironmentSheltered(game->player.position) ||
            PlayerWaterStateAt(game->player.position).eyesSubmerged;
        WeatherSetSheltered(weatherSheltered);
        WeatherUpdate(dt, game->player.position);
    } else if (!HomeWorldSurfaceIsActive() && !PlanetWorldIsActive()) {
        WeatherSetSheltered(false);
        WeatherSuspend();
    }
}

static void GameUpdatePlayerMotion(GameRuntime *game, float dt,
                                   bool inputBlocked)
{
    if (!game->perfMode && !GameWorldSimulationPaused(game) &&
        !game->landingTransition.active &&
        ShipIsDriving() && !StarMapIsOpen()) {
        if (game->debugControlEnabled) {
            ShipControlInput input = { 0 };
            if (game->scriptedShipInputFrames > 0u) {
                input = game->scriptedShipInput;
                game->scriptedShipInputFrameCarry += dt * 60.0f;
                unsigned consumed = (unsigned)floorf(
                    game->scriptedShipInputFrameCarry);
                if (consumed > game->scriptedShipInputFrames) {
                    consumed = game->scriptedShipInputFrames;
                }
                game->scriptedShipInputFrames -= consumed;
                game->scriptedShipInputFrameCarry -= (float)consumed;
            }
            ShipUpdateWithInput(&game->player, dt, &input);
        } else {
            ShipUpdate(&game->player, dt);
        }
        if (GameWorldTransitionTryLaunch(&game->player)) {
            game->wasInSpace = true;
        }
    } else if (!game->perfMode && !inputBlocked) {
        if (game->scriptedInputFrames > 0u) {
            game->appliedPlayerInput = game->scriptedPlayerInput;
            game->appliedPlayerInput.jumpPressed =
                game->scriptedInputFirstFrame &&
                game->scriptedPlayerInput.vertical > 0.0f;
            game->scriptedInputFirstFrame = false;
            game->scriptedInputFrames--;
        } else if (game->debugControlEnabled) {
            // Debug sessions are driven exclusively by stdin so a
            // desktop key held by the test runner cannot leak into the
            // simulation after a scripted input window expires.
            game->appliedPlayerInput = (PlayerInput){ 0 };
        } else {
            game->appliedPlayerInput = PlayerInputFromKeyboard();
        }
        UpdatePlayerWithInput(&game->player, dt, &game->appliedPlayerInput);
        if (PlanetWorldIsActive()) {
            game->wasInSpace = false;
        } else {
            bool launchedHome = GameWorldTransitionTryLaunch(&game->player);
            bool inSpaceNow = WorldIsSpaceActive();
            if (inSpaceNow && !game->wasInSpace) {
                if (!launchedHome) {
                    GameNoticePost(
                        "Entered space - no gravity; follow the sun to "
                        "the solar system.");
                }
            } else if (!inSpaceNow && game->wasInSpace) {
                GameNoticePost("Back in the atmosphere.");
            }
            game->wasInSpace = inSpaceNow;
        }
    }
    if (!game->landingTransition.active && WorldIsSpaceActive() &&
        !StarMapIsOpen() && SpaceRebasePlayer(&game->player)) {
        // Particles are cosmetic local-frame data; discard the old frame.
        ParticlesClear();
    }
}

static int GameUpdateWorldStreaming(GameRuntime *game,
                                    bool *localWorldActive)
{
    int effectiveRenderDistance = EffectiveRenderDistanceForHeight(
        game->player.position.y + EYE_HEIGHT);
    *localWorldActive = WorldIsSurfaceActive();
    uint32_t currentEntityDimension = WorldCurrentSurfaceId();
    if (*localWorldActive != game->entitiesWorldActive ||
        (*localWorldActive &&
         currentEntityDimension != game->entitiesWorldDimension)) {
        EntitiesClear();
        game->evolutionScanLocked = false;
        game->evolutionLockedOrganismId = 0u;
        game->entitiesWorldActive = *localWorldActive;
        game->entitiesWorldDimension = currentEntityDimension;
    }
    if (*localWorldActive) {
        UpdateChunks(game->player.position, effectiveRenderDistance);
    }
    if (WorldCurrentDimension() != WORLD_DIMENSION_PLANET) {
        SpaceProcessFinishedGenJobs();
        int spaceGenPerFrame = 2;
        if (ShipIsDriving()) {
            spaceGenPerFrame = ShipIsHighSpeedTransit()
                                   ? 16
                                   : (ShipIsCruising() ? 12 : 4);
        }
        UpdateSpaceChunks(game->player.position, effectiveRenderDistance,
                          spaceGenPerFrame);
        if (HomeWorldSurfaceIsActive() &&
            WorldCurrentDimensionAt(game->player.position.y + EYE_HEIGHT) ==
                WORLD_DIMENSION_NETHER) {
            UpdateNetherChunks(game->player.position,
                               effectiveRenderDistance, 4);
        }
        SpaceUpdateSolarGlow(game->player.position);
    }
    return effectiveRenderDistance;
}

static void GameUpdateWorldJobs(GameRuntime *game, float dt,
                                bool localWorldActive)
{
    ProcessFinishedMeshJobs(2.0);
    ProcessFinishedChunkJobs();
    if (!GameWorldSimulationPaused(game) && !game->albumOpen &&
        !game->importDialog.open && !game->landingTransition.active &&
        !game->biologyAtlasOpen && localWorldActive) {
        FluidUpdate(dt);
    }
    RebuildDirtyChunkMeshes(game->player.position);
    EffectDispatchPending();
    ParticlesUpdate(dt);
}

static void GameRenderBackground(GameRuntime *game,
                                 const GameFrameView *frame)
{
    ClearBackground(frame->skyTop);
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                           frame->skyTop, frame->skyHorizon);
    if (!frame->underwater) {
        DrawPlanetAtmosphereSky(&game->camera, &frame->planetLight,
                                &frame->planetObservation,
                                &frame->weatherVisual);
    }
}

static void GameRenderInteractionGuides(GameRuntime *game,
                                        const GameFrameView *frame)
{
    if (frame->hit.hit) {
        if (frame->hitParkedShip && !frame->hitShip.legacy) {
            Vector3 center = {
                frame->hitShip.coreX + 1.0f,
                frame->hitShip.coreY + 1.0f,
                frame->hitShip.coreZ + 1.0f
            };
            DrawCubeWires(center, 4.03f, 2.03f, 4.03f, WHITE);
        } else {
            Vector3 center = {
                frame->hit.x + 0.5f,
                frame->hit.y + 0.5f,
                frame->hit.z + 0.5f
            };
            DrawCubeWires(center, 1.03f, 1.03f, 1.03f, WHITE);
        }
    }
    if (!frame->canPlace) return;

    if (game->hotbar[game->selectedIndex] == BLOCK_SPACESHIP) {
        Vector3 center = {
            frame->placeX + 1.0f,
            frame->placeY + 1.0f,
            frame->placeZ + 1.0f
        };
        DrawCubeWires(center, 4.02f, 2.02f, 4.02f, Fade(GREEN, 0.9f));
    } else {
        Vector3 center = {
            frame->placeX + 0.5f,
            frame->placeY + 0.5f,
            frame->placeZ + 0.5f
        };
        DrawCubeWires(center, 1.02f, 1.02f, 1.02f, Fade(GREEN, 0.9f));
    }
}

static void GameRenderWorldPass(GameRuntime *game,
                                const GameFrameView *frame)
{
    PerfBeginGpuFrame();
    bool drawSurfaceChunks = PlanetWorldIsActive() ||
        (HomeWorldSurfaceIsActive() && !frame->inNether &&
         frame->spaceFade <= 0.05f);
    WorldRenderFramePrepare(&game->camera, frame->effectiveRenderDistance,
                            drawSurfaceChunks);
    DrawWorldShadowMap(&game->camera, frame->inNether,
                       &frame->worldLighting);
    BeginMode3D(game->camera);
    DrawWorld(&game->camera, frame->worldTint, frame->inNether,
              &frame->worldLighting);
    if (frame->localWorldActive) {
        EntityRendererDraw(EntitiesView(), MAX_ENTITIES);
    }
    // The ship model is only useful as an exterior reference.
    if (ShipIsDriving() && game->thirdPerson) ShipDraw(&game->player);
    if (!frame->underwater) {
        DrawHomePlanet(&game->camera, frame->spaceFade);
        if (game->showOrbitTrajectories) {
            DrawSolarOrbitTrajectories(&game->camera, frame->spaceFade);
        }
        DrawSolarBodies(&game->camera, frame->spaceFade);
    }
    if (frame->skyFade < 0.5f && !frame->inNether &&
        !frame->underwater && frame->weatherVisual.active) {
        DrawClouds(&game->camera,
                   Fade(frame->worldTint, 1.0f - frame->skyFade * 2.0f),
                   frame->weatherSimulationTime, &frame->weatherVisual,
                   &frame->environmentPresentation, &frame->worldLighting);
    }
    ParticlesDraw();
    GameRenderInteractionGuides(game, frame);
    EndMode3D();
    PerfEndGpuFrame();
}

static float GameBuildShipHud(GameRuntime *game, ShipHudState *shipHud,
                              char *systemName, size_t systemNameSize)
{
    float shipSpeed = ShipIsDriving() ? ShipRelativeSpeed() :
                                        Vector3Length(game->player.velocity);
    snprintf(systemName, systemNameSize, "---");
    *shipHud = (ShipHudState){
        .speed = shipSpeed,
        .targetSpeed = ShipTargetSpeed(),
        .closingSpeed = ShipTargetClosingSpeed(),
        .brakingDistance = ShipTargetBrakingDistance(),
        .etaSeconds = ShipTargetEtaSeconds(),
        .atmosphere = -1.0f,
        .systemName = systemName,
        .driveMode = ShipDriveModeName(),
        .approaching = ShipIsApproaching(),
        .supercruising = ShipIsSupercruising(),
        .warping = ShipIsInterstellarWarping()
    };
    if (!ShipIsDriving()) return shipSpeed;

    shipHud->cruising = ShipIsCruising();
    Vector3 gravityDir = Vector3Zero();
    float surfaceDist = 0.0f;
    if (PlanetWorldIsActive()) {
        shipHud->nearPlanet = true;
        shipHud->altitude = game->player.position.y -
            (float)PlanetTerrainHeight(
                (int)floorf(game->player.position.x),
                (int)floorf(game->player.position.z));
        shipHud->atmosphere =
            (1.0f - PlanetWorldAtmosphereFade(game->camera.position)) * 100.0f;
    } else if (HomeWorldSurfaceIsActive()) {
        shipHud->nearPlanet = true;
        shipHud->altitude = game->player.position.y -
            (float)TerrainHeight(
                (int)floorf(game->player.position.x),
                (int)floorf(game->player.position.z), WorldTerrainMode());
        shipHud->atmosphere =
            (1.0f - HomeWorldSpaceFade(game->camera.position)) * 100.0f;
    } else if (PlanetSurfaceAt(game->player.position, &gravityDir,
                               &surfaceDist, NULL)) {
        shipHud->nearPlanet = true;
        shipHud->altitude = surfaceDist;
    } else {
        shipHud->nearPlanet = false;
        shipHud->altitude = game->player.position.y - (float)SPACE_LAYER_Y;
    }
    if (shipHud->nearPlanet) {
        shipHud->subsurface = shipHud->altitude < -0.5f;
        shipHud->submerged =
            PlayerWaterStateAt(game->player.position).eyesSubmerged;
    }
    shipHud->heading = HudHeadingFromYaw(game->player.yaw);
    SolarSystemDef hudSystem = { 0 };
    float hudDistance = 0.0f;
    if (PlanetWorldIsActive()) {
        snprintf(systemName, systemNameSize, "%s surface", PlanetWorldName());
    } else if (FindNearestSystem(game->player.position,
                                 SOLAR_SYSTEM_QUERY_RADIUS,
                                 &hudSystem, &hudDistance)) {
        double distanceAu = SpaceUnitsGameDistanceToKilometers(hudDistance) /
                            SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
        snprintf(systemName, systemNameSize, "%s (%.3g AU)",
                 hudSystem.name, distanceAu);
    } else {
        snprintf(systemName, systemNameSize, "Deep space");
    }
    return shipSpeed;
}

static void GameRenderEnvironmentOverlays(GameRuntime *game,
                                          const GameFrameView *frame,
                                          const ShipHudState *shipHud)
{
    if (!frame->underwater) {
        DrawStars(&game->camera,
                  frame->inNether
                      ? 1.0f
                      : 1.0f - frame->environmentPresentation.starVisibility,
                  &frame->planetObservation, &frame->weatherVisual);
        DrawSpaceSky(frame->skyFade, frame->daylight, &game->camera);
        if (frame->spaceFade < 0.5f && !frame->inNether) {
            DrawCelestial(&game->camera, game->dayTime, frame->daylight,
                          &frame->planetLight, &frame->planetObservation,
                          &frame->weatherVisual);
        }
        if (!frame->inNether && frame->skyFade < 0.5f) {
            DrawWeatherOverlay(&game->camera, &frame->weatherVisual);
        }
    }
    DrawEnvironmentPostProcess(&frame->environmentPresentation);
    DrawWarpTunnel(&game->camera, ShipDriveTunnelIntensity(),
                   ShipIsSupercruising());
    DrawSolarGuide(&game->camera, frame->spaceFade);
    if (game->scannerActive && PlanetWorldIsActive()) {
        DrawPlanetPoiScanner(&game->camera, game->player.position);
    }
    ShipLocatorTarget shipLocatorTarget = { 0 };
    if (game->shipLocatorEnabled && !ShipIsDriving() &&
        ShipLocatorTargetAt(game->player.position, &shipLocatorTarget)) {
        DrawShipLocator(&game->camera, &shipLocatorTarget);
    }
    if (!game->paused && !HomeWorldMapIsOpen() && !StarMapIsOpen()) {
        DrawMapNavigation(game->player.position, game->player.yaw);
    }
    if (ShipIsDriving()) DrawShipHud(shipHud);
    if (frame->spaceFade > 0.05f && frame->haveAimBody &&
        !StarMapIsOpen()) {
        DrawBodyInfoPanel(&frame->aimBody);
    }
}

static void GameRenderEvolutionPanel(GameRuntime *game,
                                     const GameFrameView *frame)
{
    EntityEvolutionDebugInfo aimedEvolution = { 0 };
    int evolutionDisplayEntity = frame->entityHit;
    if (game->evolutionScanLocked) {
        evolutionDisplayEntity = EntityEvolutionFindByOrganism(
            game->evolutionLockedOrganismId);
        if (evolutionDisplayEntity < 0) {
            game->evolutionScanLocked = false;
            game->evolutionLockedOrganismId = 0u;
        }
    }
    if (EntityEvolutionInspect(evolutionDisplayEntity, &aimedEvolution)) {
        DrawEvolutionScanPanel(&aimedEvolution, game->evolutionScanLocked);
    }
}

static void GameRenderStatusHud(GameRuntime *game)
{
    DrawHotbar(game->hotbar, game->selectedIndex);
    DrawImportStatus();
    DrawStatusHUD(game->player.position, game->player.yaw,
                  game->player.pitch, game->dayTime,
                  game->autoSaveEnabled);
    if (game->cursorReleased && !game->importDialog.open) {
        DrawCursorReleasedOverlay();
    }
}

static void GameRenderHud(GameRuntime *game, const GameFrameView *frame,
                          float shipSpeed)
{
    if (!game->biologyAtlasOpen && !HomeWorldMapIsOpen()) {
        DrawCrosshair(GetScreenWidth(), GetScreenHeight());
    }
    if (!HomeWorldMapIsOpen()) GameRenderEvolutionPanel(game, frame);
    if (!game->biologyAtlasOpen && !HomeWorldMapIsOpen()) {
        GameRenderStatusHud(game);
    }
    if (game->showHelp && !game->biologyAtlasOpen &&
        !HomeWorldMapIsOpen()) {
        DrawHelpPanel(game->player.floating, game->cursorReleased,
                      ChunksRenderDistance());
    }
    DrawImportDialog(&game->importDialog);
    AlbumUiDraw();
    StarMapDraw();
    if (game->showDebug && !game->biologyAtlasOpen &&
        !HomeWorldMapIsOpen()) {
        const HudFrameState hud = {
            .dayTime = game->dayTime,
            .shipSpeed = shipSpeed,
            .targetedBlock = frame->hit.hit
                ? GetBlockAt(frame->hit.x, frame->hit.y, frame->hit.z)
                : BLOCK_AIR,
            .spaceEditCount = GetSpaceEditCount(),
            .autoSaveEnabled = game->autoSaveEnabled
        };
        DrawDebugHUD(game->player.position, game->player.yaw,
                     game->player.pitch, frame->daylight,
                     &frame->planetLight, &frame->planetObservation,
                     frame->planetSeasonProgress, &frame->weatherVisual,
                     &frame->bathymetry, &hud);
    }
}

static void GameRenderPauseOverlay(GameRuntime *game)
{
    if (!game->paused) return;

    if (IsKeyPressed(KEY_MINUS)) {
        game->settings.masterVolume = fmaxf(
            0.0f, game->settings.masterVolume - 0.1f);
        AudioSetVolumes(game->settings.masterVolume,
                        game->settings.ambientVolume,
                        game->settings.musicVolume);
        GameSettingsSave(&game->settings);
    }
    if (IsKeyPressed(KEY_EQUAL)) {
        game->settings.masterVolume = fminf(
            1.0f, game->settings.masterVolume + 0.1f);
        AudioSetVolumes(game->settings.masterVolume,
                        game->settings.ambientVolume,
                        game->settings.musicVolume);
        GameSettingsSave(&game->settings);
    }
    PauseMenuActions pauseActions = { 0 };
    PauseMenuSettings pauseSettings = {
        .graphicsQuality = game->settings.graphicsQuality,
        .masterVolume = game->settings.masterVolume,
        .ambientVolume = game->settings.ambientVolume,
        .musicVolume = game->settings.musicVolume,
        .musicEnabled = game->settings.musicEnabled
    };
    GraphicsQuality previousQuality = game->settings.graphicsQuality;
    DrawPauseMenu(&pauseSettings, &pauseActions);
    if (pauseActions.settingsChanged) {
        game->settings.graphicsQuality = pauseSettings.graphicsQuality;
        game->settings.masterVolume = pauseSettings.masterVolume;
        game->settings.ambientVolume = pauseSettings.ambientVolume;
        game->settings.musicVolume = pauseSettings.musicVolume;
        game->settings.musicEnabled = pauseSettings.musicEnabled;
        if (pauseActions.qualityChanged &&
            !WorldRendererSetQuality(game->settings.graphicsQuality)) {
            game->settings.graphicsQuality = previousQuality;
            GameNoticePost(
                "Graphics quality change failed; previous quality restored.");
        }
        WeatherSetParticleScale(
            GraphicsQualityProfileFor(
                game->settings.graphicsQuality).precipitationScale);
        AudioSetVolumes(game->settings.masterVolume,
                        game->settings.ambientVolume,
                        game->settings.musicVolume);
        AudioSetMusicEnabled(game->settings.musicEnabled);
        GameSettingsSave(&game->settings);
    }
    if (pauseActions.resume) {
        game->paused = false;
        DisableCursor();
    }
    if (pauseActions.saveWorld) {
        // Ship state can be saved even when no parking spot is available.
        GameSaveMap(&game->player);
    }
    if (pauseActions.returnToMenu) {
        game->paused = false;
        game->cursorReleased = false;
        if (game->albumOpen) {
            game->albumOpen = false;
            AlbumUiClose();
        }
        game->screen = SCREEN_START;
        AudioSetEnvironment(NULL);
        EnableCursor();
    }
    if (pauseActions.saveAndQuit) {
        GameSaveMap(&game->player);
        game->quitSaveDone = true;
        game->quitRequested = true;
    }
}

static void GameRenderFrame(GameRuntime *game,
                            const GameFrameView *frame)
{
    BeginDrawing();
    GameRenderBackground(game, frame);
    GameRenderWorldPass(game, frame);

    char shipHudSystem[48] = { 0 };
    ShipHudState shipHud = { 0 };
    float shipSpeed = GameBuildShipHud(
        game, &shipHud, shipHudSystem, sizeof(shipHudSystem));
    GameRenderEnvironmentOverlays(game, frame, &shipHud);
    GameRenderHud(game, frame, shipSpeed);
    DrawBiologyAtlas(game);
    HomeWorldMapDraw();
    DrawLandingTransitionOverlay(&game->landingTransition);
    GameRenderPauseOverlay(game);
    EndDrawing();
}
static bool GameUpdateFrame(GameRuntime *game, float dt,
                            bool debugStartRequested)
{
    if (game->perfMode) ApplyPerfRoute(&game->player, PerfFrameIndex());

    bool landingSkipPressed = LandingTransitionUpdate(&game->landingTransition, &game->player, dt);

    if (GameUpdateStartScreen(game, dt, debugStartRequested)) return false;

    if (!game->perfMode && IsKeyPressed(KEY_F10)) game->screenshotPending = true;

    GameUpdatePauseAndBiologyAtlas(game, dt, landingSkipPressed);
    GameUpdateViewModes(game);
    bool inputBlocked = GameInputBlocked(game);
    GameUpdateGameplayShortcuts(game, inputBlocked);
    GameNoticeTick(dt);
    if (!game->importDialog.open && !game->paused && !game->albumOpen) HandleImageDrop(&game->player, game->importDialog.maxBlocks, game->importDialog.relief);

    GameUpdateTemporalState(game, dt);
    GameUpdatePlayerMotion(game, dt, inputBlocked);
    bool localWorldActive = false;
    int effectiveRenderDistance =
        GameUpdateWorldStreaming(game, &localWorldActive);
    GameUpdateWorldJobs(game, dt, localWorldActive);

    GameInteractionContext interaction = { 0 };
    effectiveRenderDistance = GameUpdateInteractions(
        game, dt, inputBlocked, &interaction);

    GameFrameView frame = {
        .hit = interaction.hit,
        .hitShip = interaction.hitShip,
        .aimBody = interaction.aimBody,
        .dt = dt,
        .effectiveRenderDistance = effectiveRenderDistance,
        .entityHit = interaction.entityHit,
        .placeX = interaction.placeX,
        .placeY = interaction.placeY,
        .placeZ = interaction.placeZ,
        .localWorldActive = localWorldActive,
        .hitParkedShip = interaction.hitParkedShip,
        .haveAimBody = interaction.haveAimBody,
        .canPlace = interaction.canPlace
    };
    GameUpdateFrameEnvironment(game, &frame);
    GameRenderFrame(game, &frame);
    GameCaptureScreenshot(game, &frame);
    GameDebugTraceFrame(game, &frame);
    GameStreamAuditFrame(game);
    return true;
}

static bool GameStart(GameRuntime *game, int screenWidth, int screenHeight)
{
    const WorldExtensionHooks worldExtensionHooks = {
        .reset = FluidReset,
        .cleanup = FluidCleanup,
        .saveState = FluidSaveState,
        .loadState = FluidLoadState,
        .tryDisplaceBlock = FluidTryDisplaceForBlockTracked,
        .replayBlockDisplacement = FluidReplayBlockDisplacement,
        .onBlockChanged = FluidOnBlockChanged,
        .onBlockCommitted = GameOnWorldBlockCommitted,
        .onChunkLoaded = FluidOnChunkLoaded,
        .onChunkSectionLoaded = FluidOnChunkSectionLoaded,
        .prepareChunkSectionUnload = FluidPrepareChunkSectionUnload
    };
    WorldInstallExtensionHooks(&worldExtensionHooks);
    TerrainInstallPlanetChunkDecorator(PlanetPoiApplyToChunk);
    if (game->debugControlEnabled || game->debugTraceEnabled) {
        SetTraceLogLevel(LOG_WARNING);
    }
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Voxelcraft - raylib");
    if (!IsWindowReady()) {
        fprintf(stderr, "Failed to create a raylib window. "
                        "Run from a graphical desktop session.\n");
        return false;
    }
    if (!GameDebugTraceStart(game)) {
        CloseWindow();
        return false;
    }

    PerfConfigure(game->perfMode, game->perfReportPath,
                  game->perfBaselinePath);
    SetExitKey(KEY_NULL);
    SetTargetFPS(game->perfMode ? 0 : 60);
    EnableCursor();
    if (!ChunksStartGenThread()) {
        fprintf(stderr, "Warning: failed to start chunk generation thread; "
                        "generating synchronously.\n");
    }
    ParticlesInit();
    GameEffectsReset();
    AudioInit();
    AudioSetVolumes(game->settings.masterVolume,
                    game->settings.ambientVolume,
                    game->settings.musicVolume);
    AudioSetMusicEnabled(game->settings.musicEnabled);
    WeatherInit();
    WeatherSetParticleScale(
        GraphicsQualityProfileFor(
            game->settings.graphicsQuality).precipitationScale);
    AlbumInit();
    AlbumUiInit();
    SpaceInit();
    NetherInit();
    EntitiesInit();
    ChunksSetBlockAtlas(LoadBlockAtlas());
    WorldRendererInit(game->settings.graphicsQuality);
    SetCloudModel(LoadCloudModel());
    ShipLoadModel();
    UiFontInit();

    if (game->perfMode) {
        ChunksResetStreamingStats();
        BeginNewWorld(game, TERRAIN_VARIED, DEFAULT_WORLD_SEED);
        game->screen = SCREEN_PLAYING;
        game->autoSaveEnabled = false;
        game->showHelp = false;
        DisableCursor();
        PerfSetMetadata(
            WorldGetSeed(), EffectiveRenderDistanceForHeight(
                game->player.position.y + EYE_HEIGHT));
    }

    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL ready commands=start,screenshot,status,stream,save,load,map,marker,teleport,look,input,ship,view,"
        "fluid,evolution,quit\n");
    return true;
}

static int GameStop(GameRuntime *game)
{
    if (!game->perfMode && !game->debugControlEnabled &&
        game->screen == SCREEN_PLAYING) {
        if (game->landingTransition.active) {
            game->landingTransition.elapsed = game->landingTransition.duration;
            LandingTransitionUpdate(&game->landingTransition, &game->player,
                                    0.0f);
        }
        if (!game->quitSaveDone) GameSaveMap(&game->player);
    }
    if (!game->debugControlEnabled) GameSettingsSave(&game->settings);
    ChunksShutdownGenThread();
    WorldRenderFrameShutdown();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
    SpaceShutdown();
    UnloadAllNetherChunks();
    WorldRendererShutdown();
    UnloadTexture(ChunksBlockAtlas());
    UnloadCloudRenderResources();
    UnloadPlanetRenderResources();
    ShipCleanup();
    HomeWorldMapUnload();
    GameEffectsReset();
    AudioShutdown();
    UiFontShutdown();
    AlbumUiCleanup();
    bool perfPassed = PerfReportPassed();
    PerfShutdown();
    CloseWindow();
    WorldCleanup();
    GameDebugTraceStop(game);
    DebugControlReply(&game->debugControl, "DEBUG_CONTROL stopped\n");
    return game->perfMode && !perfPassed ? 2 : 0;
}

int GameRun(int argc, char **argv)
{
    const int screenWidth = 1280;
    const int screenHeight = 720;
    GameRuntime game;
    GameRuntimeInit(&game, argc, argv);
    if (!GameStart(&game, screenWidth, screenHeight)) return 1;

    while (!game.quitRequested && !WindowShouldClose()) {
        PerfBeginFrame();
        float dt = (game.perfMode || game.debugControlEnabled) ?
                       (1.0f / 60.0f) : GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;

        bool debugStartRequested = GameDispatchDebugCommand(&game);

        if (!GameUpdateFrame(&game, dt, debugStartRequested)) {
            GameStreamAuditFrame(&game);
            continue;
        }
        PerfEndFrame(ChunksGetStreamingStats(), CurrentRenderResourceSnapshot());
        if (game.perfMode && PerfReportWritten() && !game.debugControlEnabled) {
            game.quitRequested = true;
        }
    }

    return GameStop(&game);
}
