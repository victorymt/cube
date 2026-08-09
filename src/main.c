#include "raylib.h"
#include "raymath.h"

#include "types.h"
#include "terrain.h"
#include "world.h"
#include "chunks.h"
#include "player.h"
#include "interaction.h"
#include "album.h"
#include "inventory.h"
#include "render.h"
#include "particles.h"
#include "audio.h"
#include "weather.h"
#include "space.h"
#include "world_environment.h"
#include "ship.h"
#include "nether.h"
#include "entity.h"
#include "starmap.h"
#include "discovery.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

TerrainMode terrainMode = TERRAIN_VARIED;
static bool autoSaveEnabled = true;
static float autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
static float dayTime = 0.30f;
static bool dayCycleEnabled = true;

static bool FindLandingSpot(Vector3 start, int minY, int maxY, Vector3 *out)
{
    Vector3 spot = start;
    if (spot.y < (float)minY) spot.y = (float)minY;
    if (spot.y > (float)maxY) spot.y = (float)maxY;

    int safety = 0;
    while (PlayerOverlapsWorld(spot) && safety < 12) {
        spot.y += 1.0f;
        safety++;
        if (spot.y > (float)maxY) spot.y = (float)maxY;
    }
    if (PlayerOverlapsWorld(spot)) return false;

    while (spot.y > (float)minY) {
        Vector3 below = spot;
        below.y -= 1.0f;
        if (below.y < (float)minY || PlayerOverlapsWorld(below)) break;
        spot = below;
    }
    *out = spot;
    return true;
}

static void BeginNewWorld(Player *player, TerrainMode mode, uint32_t seed)
{
    DrainChunkGen();
    UnloadAllChunks();
    SpaceReset();
    NetherReset();
    AlbumReset();
    WorldReset(seed);
    InventoryReset();
    InventoryGrantStarterKit();
    ShipReset();
    StarMapClose();
    EntitiesClear();
    ParticlesClear();
    WeatherInit();

    terrainMode = mode;
    player->position = (Vector3){ 0.5f, (float)TerrainHeight(0, 0, terrainMode) + 3.0f, 0.5f };
    player->velocity = Vector3Zero();
    player->yaw = PI;
    player->pitch = -0.25f;
    player->onGround = false;
    player->floating = false;

    autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
    dayTime = 0.30f;
    dayCycleEnabled = true;
    UpdateChunks(player->position, EffectiveRenderDistanceForHeight(player->position.y + EYE_HEIGHT));
}

int main(void)
{
    const int screenWidth = 1280;
    const int screenHeight = 720;

    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Voxelcraft - raylib");
    if (!IsWindowReady()) {
        fprintf(stderr, "Failed to create a raylib window. Run from a graphical desktop session.\n");
        return 1;
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    EnableCursor();
    if (!ChunksStartGenThread()) {
        fprintf(stderr, "Warning: failed to start chunk generation thread; generating synchronously.\n");
    }
    ParticlesInit();
    AudioInit();
    WeatherInit();
    AlbumInit();
    SpaceInit();
    NetherInit();
    EntitiesInit();
    blockAtlas = LoadBlockAtlas();
    cloudModel = LoadCloudModel();
    ShipLoadModel();

    Player player = {
        .position = { 0.5f, 12.0f, 0.5f },
        .velocity = { 0.0f, 0.0f, 0.0f },
        .yaw = PI,
        .pitch = -0.25f,
        .onGround = false,
        .floating = false
    };

    BlockType hotbar[HOTBAR_SIZE] = {
        BLOCK_GRASS, BLOCK_DIRT, BLOCK_STONE, BLOCK_WOOD, BLOCK_PLANK,
        BLOCK_SAND, BLOCK_SNOW, BLOCK_GLASS, BLOCK_WATER, BLOCK_SPACESHIP
    };
    int selectedIndex = 0;
    bool showHelp = true;
    bool showDebug = false;
    bool scannerActive = false;
    bool showOrbitTrajectories = true;
    int screenshotCounter = 0;
    bool quitRequested = false;
    bool cursorReleased = false;
    bool paused = false;
    bool albumOpen = false;
    bool albumRainSuspended = false;
    bool wasInSpace = false;
    bool entitiesWorldActive = true;
    uint32_t entitiesWorldDimension = 0u;
    bool thirdPerson = false;
    ImportDialog importDialog = {
        .relief = true,
        .maxBlocks = IMPORT_DEFAULT_BLOCKS
    };
    GameScreen screen = SCREEN_START;
    TerrainMode selectedTerrain = TERRAIN_VARIED;
    uint32_t selectedSeed = DEFAULT_WORLD_SEED;

    Camera3D camera = { 0 };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = CameraFovForHeight(player.position.y + EYE_HEIGHT);
    camera.projection = CAMERA_PERSPECTIVE;

    while (!quitRequested && !WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;

        if (screen == SCREEN_START) {
            bool startGame = false;
            if (IsKeyPressed(KEY_ESCAPE)) quitRequested = true;
            BeginDrawing();
            DrawStartPage(&startGame, &quitRequested, &selectedTerrain, &selectedSeed);
            EndDrawing();

            if (startGame) {
                BeginNewWorld(&player, selectedTerrain, selectedSeed);
                importDialog.open = false;
                importDialog.relief = true;
                importDialog.maxBlocks = IMPORT_DEFAULT_BLOCKS;
                importDialog.path[0] = '\0';
                albumOpen = false;
                albumRainSuspended = false;
                wasInSpace = false;
                entitiesWorldActive = true;
                entitiesWorldDimension = 0u;
                thirdPerson = false;
                paused = false;
                screen = SCREEN_PLAYING;
                cursorReleased = false;
                DisableCursor();
                SetImportMessage(terrainMode == TERRAIN_FLAT ?
                                 TextFormat("Flat world seed %u. Press I to import.", WorldGetSeed()) :
                                 TextFormat("World seed %u.", WorldGetSeed()));
            }
            continue;
        }

        if (IsKeyPressed(KEY_F10)) {
            TakeScreenshot(TextFormat("voxelcraft_shot_%03d.png", screenshotCounter));
            SetImportMessage(TextFormat("Screenshot saved: voxelcraft_shot_%03d.png", screenshotCounter));
            screenshotCounter++;
        }

        if (!importDialog.open && !albumOpen && !StarMapIsOpen()) {
            if (!paused && IsKeyPressed(KEY_ESCAPE)) {
                paused = true;
                player.velocity = Vector3Zero();
                cursorReleased = false;
                EnableCursor();
            } else if (paused && (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER))) {
                paused = false;
                DisableCursor();
            }
        }

        if (!paused && !albumOpen && !importDialog.open) SpaceAdvanceTime(dt);

        if (!albumOpen && !importDialog.open && !paused && IsKeyPressed(KEY_P)) {
            if (WeatherGetCurrent() == WEATHER_RAIN) {
                albumRainSuspended = true;
                AudioSetRain(false);
            }
            AlbumOpen();
            albumOpen = true;
            player.velocity = Vector3Zero();
            cursorReleased = false;
            EnableCursor();
        }
        AlbumUpdate();
        if (AlbumConsumePlaceRequest()) {
            const char *placedPath = AlbumSelectedPath();
            AlbumClose();
            albumOpen = false;
            if (albumRainSuspended) {
                albumRainSuspended = false;
                AudioSetRain(true);
            }
            if (!paused && !cursorReleased && !importDialog.open) DisableCursor();
            if (placedPath) {
                ImportImageAsBlocks(placedPath, &player, IMPORT_DEFAULT_BLOCKS, false);
            }
        }
        if (!AlbumIsOpen() && albumOpen) {
            albumOpen = false;
            if (albumRainSuspended) {
                albumRainSuspended = false;
                AudioSetRain(true);
            }
            if (!paused && !cursorReleased && !importDialog.open) DisableCursor();
        }

        if (!importDialog.open && !paused && !albumOpen && IsKeyPressed(KEY_TAB)) {
            cursorReleased = !cursorReleased;
            if (cursorReleased) {
                player.velocity = Vector3Zero();
                EnableCursor();
            } else {
                DisableCursor();
            }
        }
        if (ShipIsDriving() && IsKeyPressed(KEY_E)) {
            ShipExit(&player);
        }
        if (WorldCurrentDimension() != WORLD_DIMENSION_PLANET &&
            IsKeyPressed(KEY_M) && !StarMapIsOpen() && !paused && !cursorReleased) {
            StarMapOpen();
            player.velocity = Vector3Zero();
            cursorReleased = true;
            EnableCursor();
        }
        if (StarMapIsOpen()) {
            SolarSystemDef destination = { 0 };
            StarMapUpdate(player.position);
            if (StarMapConsumeTravel(&destination)) {
                ShipBeginSystemWarp(&player, destination.anchorX, destination.anchorZ);
                StarMapClose();
                cursorReleased = false;
                DisableCursor();
            }
            if (!StarMapIsOpen()) {
                cursorReleased = false;
                DisableCursor();
            }
        }
        if (!paused && !albumOpen && !importDialog.open && IsKeyPressed(KEY_F4)) {
            thirdPerson = !thirdPerson;
            SetImportMessage(thirdPerson ? "Third person view." : "First person view.");
        }
        bool openedImportDialog = false;
        if (!importDialog.open && !paused && IsKeyPressed(KEY_I)) {
            OpenImportDialog(&importDialog);
            if (importDialog.open) {
                openedImportDialog = true;
                cursorReleased = true;
                player.velocity = Vector3Zero();
                EnableCursor();
            }
        }
        if (!openedImportDialog) UpdateImportDialog(&importDialog, &player, &cursorReleased);

        bool inputBlocked = paused || cursorReleased || importDialog.open || albumOpen ||
                            ShipIsDriving() || StarMapIsOpen();
        if (!paused && !albumOpen && !importDialog.open && !StarMapIsOpen() &&
            IsKeyPressed(KEY_O)) {
            showOrbitTrajectories = !showOrbitTrajectories;
            SetImportMessage(showOrbitTrajectories ? "Orbit trajectories shown."
                                                   : "Orbit trajectories hidden.");
        }
        if (!inputBlocked && IsKeyPressed(KEY_F1)) showHelp = !showHelp;
        if (!inputBlocked && IsKeyPressed(KEY_F3)) showDebug = !showDebug;
        if (!inputBlocked) {
            int hotbarKey = HotbarKeyToIndex();
            if (hotbarKey >= 0 && hotbarKey < HOTBAR_SIZE) selectedIndex = hotbarKey;
            float wheel = GetMouseWheelMove();
            if (wheel > 0.0f) selectedIndex = (selectedIndex + HOTBAR_SIZE - 1) % HOTBAR_SIZE;
            else if (wheel < 0.0f) selectedIndex = (selectedIndex + 1) % HOTBAR_SIZE;
            if (IsKeyPressed(KEY_LEFT_BRACKET)) AdjustRenderDistance(-1);
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) AdjustRenderDistance(1);
            if (IsKeyPressed(KEY_F5)) SaveMap(&player);
            if (IsKeyPressed(KEY_F9)) {
                LoadMap(&player);
                wasInSpace = WorldIsSpaceActive();
                cursorReleased = false;
                DisableCursor();
                autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
            }
            if (IsKeyPressed(KEY_F6)) {
                dayCycleEnabled = !dayCycleEnabled;
                SetImportMessage(dayCycleEnabled ? "Day/night cycle enabled." : "Day/night cycle paused.");
            }
            if (IsKeyPressed(KEY_F7)) {
                Biome playerBiome = BiomeAt((int)floorf(player.position.x), (int)floorf(player.position.z));
                bool coldArea = playerBiome == BIOME_SNOW || playerBiome == BIOME_MOUNTAIN || player.position.y > 24.0f;
                WeatherCycle(coldArea);
                SetImportMessage(TextFormat("Weather: %s", WeatherName()));
            }
            if (IsKeyPressed(KEY_F8)) {
                autoSaveEnabled = !autoSaveEnabled;
                autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
                SetImportMessage(autoSaveEnabled ? "Auto-save enabled (every 60s)." : "Auto-save disabled.");
            }
            if (PlanetWorldIsActive() && IsKeyPressed(KEY_C)) {
                scannerActive = !scannerActive;
                if (scannerActive) {
                    PlanetPoi poi = { 0 };
                    if (PlanetPoiNearest(player.position, &poi)) {
                        SetImportMessage(TextFormat("Scanner online: %s", poi.name));
                    } else {
                        SetImportMessage("Scanner online: no signal found.");
                    }
                } else {
                    SetImportMessage("Scanner offline.");
                }
            }
            bool ctrlHeld = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            if (ctrlHeld && IsKeyPressed(KEY_Z) && !IsKeyDown(KEY_LEFT_SHIFT)) {
                if (UndoBlockEdit()) SetImportMessage("Undo");
            } else if (ctrlHeld && (IsKeyPressed(KEY_Y) || (IsKeyDown(KEY_LEFT_SHIFT) && IsKeyPressed(KEY_Z)))) {
                if (RedoBlockEdit()) SetImportMessage("Redo");
            }
        }
        WorldTickImportMessage(dt);
        if (!importDialog.open && !paused && !albumOpen) HandleImageDrop(&player, importDialog.maxBlocks, importDialog.relief);

        if (autoSaveEnabled && screen == SCREEN_PLAYING && !paused) {
            autoSaveTimer -= dt;
            if (autoSaveTimer <= 0.0f) {
                autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
                SaveMap(&player);
            }
        }

        if (dayCycleEnabled && !paused && !albumOpen) {
            dayTime += dt / DAY_LENGTH_SECONDS;
            if (dayTime >= 1.0f) dayTime -= 1.0f;
        }
        if (!paused && !albumOpen && HomeWorldSurfaceIsActive()) {
            Biome playerBiome = BiomeAt((int)floorf(player.position.x), (int)floorf(player.position.z));
            bool coldArea = playerBiome == BIOME_SNOW || playerBiome == BIOME_MOUNTAIN || player.position.y > 24.0f;
            WeatherUpdate(dt, player.position, coldArea);
        }

        AudioUpdate();

        if (ShipIsDriving() && !StarMapIsOpen()) {
            ShipUpdate(&player, dt);
            if (PlanetWorldTryLaunch(&player) || HomeWorldTryLaunch(&player)) {
                wasInSpace = true;
            }
        } else if (!inputBlocked) {
            UpdatePlayer(&player, dt);
            if (PlanetWorldIsActive()) {
                wasInSpace = false;
            } else {
                bool launchedHome = HomeWorldTryLaunch(&player);
                bool inSpaceNow = WorldIsSpaceActive();
                if (inSpaceNow && !wasInSpace) {
                    if (!launchedHome) {
                        SetImportMessage("Entered space - no gravity; follow the sun to the solar system.");
                    }
                } else if (!inSpaceNow && wasInSpace) {
                    SetImportMessage("Back in the atmosphere.");
                }
                wasInSpace = inSpaceNow;
            }
        }
        if (WorldIsSpaceActive() && !StarMapIsOpen() &&
            SpaceRebasePlayer(&player)) {
            // Particles are cosmetic local-frame data; discard the old frame.
            ParticlesClear();
        }
        int effectiveRenderDistance = EffectiveRenderDistanceForHeight(player.position.y + EYE_HEIGHT);
        bool localWorldActive = WorldIsSurfaceActive();
        uint32_t currentEntityDimension = WorldCurrentSurfaceId();
        if (localWorldActive != entitiesWorldActive ||
            (localWorldActive && currentEntityDimension != entitiesWorldDimension)) {
            EntitiesClear();
            entitiesWorldActive = localWorldActive;
            entitiesWorldDimension = currentEntityDimension;
        }
        if (localWorldActive) UpdateChunks(player.position, effectiveRenderDistance);
        if (WorldCurrentDimension() != WORLD_DIMENSION_PLANET) {
            SpaceProcessFinishedGenJobs();
            int spaceGenPerFrame = 2;
            if (ShipIsDriving()) {
                spaceGenPerFrame = ShipIsWarping() ? 16 : (ShipIsCruising() ? 12 : 4);
            }
            UpdateSpaceChunks(player.position, effectiveRenderDistance, spaceGenPerFrame);
            if (HomeWorldSurfaceIsActive()) {
                UpdateNetherChunks(player.position, effectiveRenderDistance, 4);
            }
            SpaceUpdateStarGlow(player.position);
            SpaceUpdateSolarGlow(player.position);
        }
        ProcessFinishedMeshJobs();
        ProcessFinishedChunkJobs();
        RebuildDirtyChunkMeshes();
        ParticlesUpdate(dt);

        UpdatePlayerCamera(&camera, &player, dt, thirdPerson);
        effectiveRenderDistance = EffectiveRenderDistanceForHeight(camera.position.y);

        Vector3 aimEye = { player.position.x, player.position.y + EYE_HEIGHT, player.position.z };
        Vector3 aimDir = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        HitResult hit = RaycastBlocks(aimEye, aimDir, REACH_DISTANCE);
        int entityHit = EntityRayHit(aimEye, aimDir, REACH_DISTANCE);
        SpaceBodyInfo aimBody = { 0 };
        bool haveAimBody = SpaceBodyPick(aimEye, aimDir, &aimBody);
        if (!inputBlocked && entityHit >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            EntityKill(entityHit);
        } else if (!inputBlocked && hit.hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hit.y >= NETHER_LAYER_Y) {
            BlockType brokenType = GetBlockAt(hit.x, hit.y, hit.z);
            PlanetPoi claimedPoi = { 0 };
            bool poiCore = PlanetPoiIsCore(hit.x, hit.y, hit.z);
            bool poiClaimed = PlanetPoiIsClaimed(hit.x, hit.y, hit.z);
            if (PlanetPoiTryClaim(hit.x, hit.y, hit.z, &claimedPoi)) {
                ParticlesEmitBurst((Vector3){ hit.x + 0.5f, hit.y + 0.5f, hit.z + 0.5f },
                                   BlockBaseColor(claimedPoi.rewardBlock), 24, 3.8f, 0.85f);
                AudioPlayBreak();
                SetImportMessage(TextFormat("Survey complete: %s, +%d %s", claimedPoi.name,
                                            claimedPoi.rewardAmount,
                                            BlockName(claimedPoi.rewardBlock)));
            } else if (poiClaimed) {
                SetImportMessage("This discovery has already been catalogued.");
            } else if (!poiCore && brokenType != BLOCK_AIR && InventoryAdd(brokenType, 1) > 0) {
                ParticlesEmitBurst((Vector3){ hit.x + 0.5f, hit.y + 0.5f, hit.z + 0.5f },
                                   BlockBaseColor(brokenType), 16, 3.0f, 0.7f);
                AudioPlayBreak();
                SetBlock(hit.x, hit.y, hit.z, BLOCK_AIR);
            } else if (!poiCore && brokenType != BLOCK_AIR) {
                SetImportMessage(TextFormat("Inventory full: %s", BlockName(brokenType)));
            }
        }
        int placeX = 0;
        int placeY = 0;
        int placeZ = 0;
        bool canPlace = false;
        if (!inputBlocked && hit.hit) {
            placeX = hit.x + hit.nx;
            placeY = hit.y + hit.ny;
            placeZ = hit.z + hit.nz;
            canPlace = InventoryCount(hotbar[selectedIndex]) > 0 &&
                       GetBlockAt(placeX, placeY, placeZ) == BLOCK_AIR &&
                       WorldBlockRegionAt(placeY) != WORLD_BLOCK_REGION_NONE &&
                       !BlockWouldOverlapPlayer(placeX, placeY, placeZ, player.position);
        }
        if (!inputBlocked && hit.hit && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            if (GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_ALBUM) {
                if (WeatherGetCurrent() == WEATHER_RAIN) {
                    albumRainSuspended = true;
                    AudioSetRain(false);
                }
                AlbumOpen();
                albumOpen = true;
                player.velocity = Vector3Zero();
                cursorReleased = false;
                EnableCursor();
            } else if (!ShipIsDriving() && ShipTryEnter(hit.x, hit.y, hit.z, &player)) {
            } else if (GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_NETHER_PORTAL) {
                Vector3 landing = player.position;
                if (player.position.y > 0.0f) {
                    landing.y = -46.0f;
                    FindLandingSpot(landing, NETHER_LAYER_Y + 1, NETHER_LAYER_TOP - 1, &landing);
                    SetImportMessage("Entered the Nether.");
                } else {
                    float groundY = (float)TerrainHeight((int)floorf(player.position.x),
                                                         (int)floorf(player.position.z), terrainMode);
                    landing.y = groundY + 3.0f;
                    FindLandingSpot(landing, 0, WORLD_HEIGHT - 1, &landing);
                    SetImportMessage("Back to the surface.");
                }
                player.position = landing;
                player.velocity = Vector3Zero();
                player.floating = false;
                wasInSpace = false;
            } else if (GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_DOOR ||
                       GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_DOOR_OPEN) {
                BlockType doorType = GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_DOOR ? BLOCK_DOOR_OPEN : BLOCK_DOOR;
                AudioPlayPlace();
                SetBlock(hit.x, hit.y, hit.z, doorType);
            } else if (GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_FENCE_GATE ||
                       GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_FENCE_GATE_OPEN) {
                BlockType gateType = GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_FENCE_GATE ? BLOCK_FENCE_GATE_OPEN : BLOCK_FENCE_GATE;
                AudioPlayPlace();
                SetBlock(hit.x, hit.y, hit.z, gateType);
            } else if (canPlace) {
                BlockType placedType = hotbar[selectedIndex];
                if (InventoryConsume(placedType, 1)) {
                    ParticlesEmitBurst((Vector3){ placeX + 0.5f, placeY + 0.5f, placeZ + 0.5f },
                                       BlockBaseColor(placedType), 8, 2.0f, 0.5f);
                    AudioPlayPlace();
                    SetBlock(placeX, placeY, placeZ, placedType);
                }
            }
        }
        if (!inputBlocked && hit.hit && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
            BlockType picked = GetBlockAt(hit.x, hit.y, hit.z);
            if (picked != BLOCK_AIR && IsValidBlockType(picked)) {
                hotbar[selectedIndex] = picked;
                AudioPlayPick();
                SetImportMessage(TextFormat("Picked %s (%d)", BlockName(picked), InventoryCount(picked)));
            }
        }

        float daylight = 0.0f;
        float sunset = 0.0f;
        PlanetLightState planetLight = { 0 };
        if (!PlanetWorldLightStateAt(player.position, &planetLight)) {
            DayNightFactors(dayTime, &daylight, &sunset);
        } else {
            daylight = planetLight.daylight;
            sunset = planetLight.sunset;
        }
        if (!paused && !albumOpen && !importDialog.open && localWorldActive) {
            EntitiesUpdate(dt, &player, daylight);
        }
        float spaceFade = HomeWorldSpaceFade(camera.position);
        Color skyTop = { 0 };
        Color skyHorizon = { 0 };
        SkyColorsForLight(daylight, sunset, &skyTop, &skyHorizon);
        Color worldTint = MixWeather(WorldTintForLight(daylight, sunset), daylight);
        skyTop = MixWeather(skyTop, daylight);
        skyHorizon = MixWeather(skyHorizon, daylight);
        ApplyPlanetWorldPaletteWithLight(&skyTop, &skyHorizon, &worldTint,
                                         &planetLight);
        skyTop = ColorLerp(skyTop, BLACK, spaceFade);
        skyHorizon = ColorLerp(skyHorizon, BLACK, spaceFade);
        worldTint = ColorLerp(worldTint, (Color){ 46, 54, 78, 255 }, spaceFade);
        bool inNether = WorldCurrentDimensionAt(camera.position.y) == WORLD_DIMENSION_NETHER;
        if (inNether) {
            skyTop = (Color){ 24, 6, 6, 255 };
            skyHorizon = (Color){ 40, 10, 8, 255 };
            worldTint = (Color){ 150, 62, 42, 255 };
            spaceFade = 0.0f;
        }

        BeginDrawing();
        ClearBackground(skyTop);
        DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), skyTop, skyHorizon);
        DrawPlanetAtmosphereSky(&camera, &planetLight);

        BeginMode3D(camera);
        bool drawSurfaceChunks = PlanetWorldIsActive() ||
                                 (HomeWorldSurfaceIsActive() && spaceFade <= 0.05f);
        DrawWorld(&camera, effectiveRenderDistance, worldTint, drawSurfaceChunks,
                  HomeWorldSurfaceIsActive());
        if (localWorldActive) EntitiesDraw();
        // Keep the first-person flight view clear. The ship model is only useful
        // as an exterior reference when the camera is in third person.
        if (ShipIsDriving() && thirdPerson) ShipDraw(&player);
        DrawHomePlanet(&camera, spaceFade);
        if (showOrbitTrajectories) DrawSolarOrbitTrajectories(&camera, spaceFade);
        DrawSolarBodies(&camera, spaceFade);
        bool drawCloudLayer = HomeWorldSurfaceIsActive();
        if (PlanetWorldIsActive()) {
            const PlanetProfile *profile = PlanetWorldProfile();
            drawCloudLayer = profile->atmosphereType != PLANET_ATMOSPHERE_NONE &&
                             profile->atmosphereDensity > 0.28f;
        }
        if (spaceFade < 0.5f && !inNether && drawCloudLayer) {
            DrawClouds(&camera, Fade(worldTint, 1.0f - spaceFade * 2.0f));
        }
        ParticlesDraw();
        if (hit.hit) {
            Vector3 center = { hit.x + 0.5f, hit.y + 0.5f, hit.z + 0.5f };
            DrawCubeWires(center, 1.03f, 1.03f, 1.03f, WHITE);
        }
        if (canPlace) {
            Vector3 center = { placeX + 0.5f, placeY + 0.5f, placeZ + 0.5f };
            DrawCubeWires(center, 1.02f, 1.02f, 1.02f, Fade(GREEN, 0.9f));
        }
        EndMode3D();

        if (IsWaterBlock(GetBlock((int)floorf(camera.position.x),
                                  (int)floorf(camera.position.y),
                                  (int)floorf(camera.position.z)))) {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), (Color){ 16, 64, 128, 130 });
        }

        DrawStars(&camera, inNether ? 1.0f : daylight * (1.0f - spaceFade));
        DrawSpaceSky(spaceFade, &camera);
        DrawSolarGuide(&camera, spaceFade);
        if (scannerActive && PlanetWorldIsActive()) PlanetPoiDrawScanner(&camera, player.position);
        if (ShipIsDriving()) DrawShipHud();
        if (spaceFade > 0.05f && haveAimBody && !StarMapIsOpen()) {
            DrawBodyInfoPanel(&aimBody);
        }
        if (spaceFade < 0.5f && !inNether) DrawCelestial(&camera, dayTime, daylight);
        DrawCrosshair(GetScreenWidth(), GetScreenHeight());
        DrawHotbar(hotbar, selectedIndex);
        DrawImportStatus();
        int hour = (int)(dayTime * 24.0f) % 24;
        const char *positionText = TextFormat("XYZ %d %d %d    %02d:00", (int)floorf(player.position.x),
                                              (int)floorf(player.position.y),
                                              (int)floorf(player.position.z), hour);
        DrawText(positionText, 15, GetScreenHeight() - 32, 17, Fade(BLACK, 0.92f));
        DrawText(positionText, 14, GetScreenHeight() - 34, 17, Fade(WHITE, 0.9f));
        const char *saveText = TextFormat("Auto-save: %s", autoSaveEnabled ? "60s" : "off");
        DrawText(saveText, 15, GetScreenHeight() - 14, 15, Fade(BLACK, 0.92f));
        DrawText(saveText, 14, GetScreenHeight() - 16, 15, Fade(WHITE, 0.65f));
        if (cursorReleased && !importDialog.open) DrawCursorReleasedOverlay();
        if (showHelp) DrawHelpPanel(player.floating, cursorReleased, renderDistanceChunks);
        DrawImportDialog(&importDialog);
        AlbumDraw();
        StarMapDraw();
        if (showDebug) {
            dayTimeForHud = dayTime;
            autoSaveForHud = autoSaveEnabled;
            blockForHud = hit.hit ? GetBlockAt(hit.x, hit.y, hit.z) : BLOCK_AIR;
            SpaceEditCountForHud = GetSpaceEditCount();
            shipSpeedForHud = Vector3Length(player.velocity);
            if (ShipIsDriving()) {
                shipHudSpeed = shipSpeedForHud;
                shipHudCruising = ShipIsCruising();
                Vector3 gravityDir = Vector3Zero();
                float surfaceDist = 0.0f;
                if (PlanetWorldIsActive()) {
                    shipHudNearPlanet = true;
                    shipHudAlt = player.position.y -
                                 (float)PlanetTerrainHeight((int)floorf(player.position.x),
                                                            (int)floorf(player.position.z));
                } else if (HomeWorldSurfaceIsActive()) {
                    shipHudNearPlanet = true;
                    shipHudAlt = player.position.y -
                                 (float)TerrainHeight((int)floorf(player.position.x),
                                                      (int)floorf(player.position.z), terrainMode);
                } else if (PlanetSurfaceAt(player.position, &gravityDir, &surfaceDist,
                                           NULL)) {
                    shipHudNearPlanet = true;
                    shipHudAlt = surfaceDist;
                } else {
                    shipHudNearPlanet = false;
                    shipHudAlt = player.position.y - (float)SPACE_LAYER_Y;
                }
                shipHudHeading = fmodf(player.yaw * RAD2DEG + 360.0f, 360.0f);
                SolarSystemDef hudSys;
                float hudDist = 0.0f;
                if (PlanetWorldIsActive()) {
                    snprintf(shipHudSystem, sizeof(shipHudSystem), "%s surface", PlanetWorldName());
                } else if (FindNearestSystem(player.position, 3000.0f, &hudSys, &hudDist)) {
                    snprintf(shipHudSystem, sizeof(shipHudSystem), "%s Prime (%.0f)", hudSys.name, hudDist);
                } else {
                    snprintf(shipHudSystem, sizeof(shipHudSystem), "Deep space");
                }
            }
            DrawDebugHUD(player.position, player.yaw, player.pitch);
        }
        if (paused) {
            if (IsKeyPressed(KEY_MINUS)) SetMasterVolume(fmaxf(0.0f, GetMasterVolume() - 0.1f));
            if (IsKeyPressed(KEY_EQUAL)) SetMasterVolume(fminf(1.0f, GetMasterVolume() + 0.1f));
            bool resumeGame = false;
            bool saveWorld = false;
            bool saveAndQuit = false;
            bool toggleMusic = false;
            bool returnToMenu = false;
            DrawPauseMenu(&resumeGame, &saveWorld, &saveAndQuit, &toggleMusic, &returnToMenu);
            if (resumeGame) {
                paused = false;
                DisableCursor();
            }
            if (toggleMusic) AudioToggleMusic();
            if (saveWorld) {
                if (ShipIsDriving()) ShipForceExit(&player);
                SaveMap(&player);
            }
            if (returnToMenu) {
                paused = false;
                cursorReleased = false;
                if (albumOpen) {
                    albumOpen = false;
                    AlbumClose();
                }
                screen = SCREEN_START;
                EnableCursor();
            }
            if (saveAndQuit) {
                if (ShipIsDriving()) ShipForceExit(&player);
                SaveMap(&player);
                quitRequested = true;
            }
        }

        EndDrawing();
    }

    if (screen == SCREEN_PLAYING) {
        if (ShipIsDriving()) ShipForceExit(&player);
        SaveMap(&player);
    }
    ChunksShutdownGenThread();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
    SpaceShutdown();
    UnloadAllNetherChunks();
    UnloadTexture(blockAtlas);
    if (cloudModel.meshCount > 0) UnloadModel(cloudModel);
    UnloadPlanetRenderResources();
    ShipCleanup();
    AudioShutdown();
    CloseWindow();
    AlbumCleanup();
    WorldCleanup();
    return 0;
}
