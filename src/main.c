#include "raylib.h"
#include "raymath.h"

#include "types.h"
#include "terrain.h"
#include "world.h"
#include "chunks.h"
#include "player.h"
#include "interaction.h"
#include "album.h"
#include "render.h"
#include "particles.h"
#include "audio.h"
#include "weather.h"
#include "space.h"
#include "ship.h"
#include "nether.h"
#include "entity.h"
#include "starmap.h"

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
        BLOCK_BRICK, BLOCK_SAND, BLOCK_SNOW, BLOCK_GLASS, BLOCK_WATER
    };
    int selectedIndex = 0;
    bool showHelp = true;
    bool showDebug = false;
    int screenshotCounter = 0;
    bool quitRequested = false;
    bool cursorReleased = false;
    bool paused = false;
    bool albumOpen = false;
    bool albumRainSuspended = false;
    bool wasInSpace = false;
    bool thirdPerson = false;
    ImportDialog importDialog = {
        .relief = true,
        .maxBlocks = IMPORT_DEFAULT_BLOCKS
    };
    GameScreen screen = SCREEN_START;
    TerrainMode selectedTerrain = TERRAIN_VARIED;

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
            DrawStartPage(&startGame, &quitRequested, &selectedTerrain);
            EndDrawing();

            if (startGame) {
                terrainMode = selectedTerrain;
                player.position = (Vector3){ 0.5f, (float)TerrainHeight(0, 0, terrainMode) + 3.0f, 0.5f };
                player.velocity = Vector3Zero();
                player.onGround = false;
                player.floating = false;
                importDialog.open = false;
                importDialog.relief = true;
                importDialog.maxBlocks = IMPORT_DEFAULT_BLOCKS;
                importDialog.path[0] = '\0';
                UpdateChunks(player.position, EffectiveRenderDistanceForHeight(player.position.y + EYE_HEIGHT));
                screen = SCREEN_PLAYING;
                cursorReleased = false;
                DisableCursor();
                SetImportMessage(terrainMode == TERRAIN_FLAT ?
                                 "Flat mode: press I to import, F5 save, F9 load." :
                                 "Press F5 to save, F9 to load.");
            }
            continue;
        }

        if (IsKeyPressed(KEY_F10)) {
            TakeScreenshot(TextFormat("voxelcraft_shot_%03d.png", screenshotCounter));
            SetImportMessage(TextFormat("Screenshot saved: voxelcraft_shot_%03d.png", screenshotCounter));
            screenshotCounter++;
        }

        if (!importDialog.open && !albumOpen) {
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
        if (ShipIsDriving() && IsKeyPressed(KEY_TAB) && !StarMapIsOpen() && !paused) {
            StarMapOpen();
        }
        if (StarMapIsOpen()) {
            Vector3 destination = { 0 };
            StarMapUpdate(player.position);
            if (StarMapConsumeTravel(&destination)) {
                player.position = destination;
                player.velocity = Vector3Zero();
                player.floating = false;
                SetImportMessage(TextFormat("Arrived at %.1f, %.1f, %.1f - explore the system.",
                                            destination.x, destination.y, destination.z));
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
        if (!paused && !albumOpen) {
            Biome playerBiome = BiomeAt((int)floorf(player.position.x), (int)floorf(player.position.z));
            bool coldArea = playerBiome == BIOME_SNOW || playerBiome == BIOME_MOUNTAIN || player.position.y > 24.0f;
            WeatherUpdate(dt, player.position, coldArea);
        }

        AudioUpdate();

        if (ShipIsDriving() && !StarMapIsOpen()) {
            ShipUpdate(&player, dt);
        } else if (!inputBlocked) {
            UpdatePlayer(&player, dt);
            bool inSpaceNow = player.position.y >= SPACE_ENTER_Y;
            if (inSpaceNow && !wasInSpace) {
                SetImportMessage("Entered space - no gravity; follow the sun to the solar system.");
            } else if (!inSpaceNow && wasInSpace && player.position.y < SPACE_EXIT_Y) {
                SetImportMessage("Back in the atmosphere.");
            }
            wasInSpace = inSpaceNow;
        }
        int effectiveRenderDistance = EffectiveRenderDistanceForHeight(player.position.y + EYE_HEIGHT);
        UpdateChunks(player.position, effectiveRenderDistance);
        int spaceGenPerFrame = 2;
        if (ShipIsDriving()) spaceGenPerFrame = ShipIsCruising() ? 12 : 4;
        UpdateSpaceChunks(player.position, effectiveRenderDistance, spaceGenPerFrame);
        UpdateNetherChunks(player.position, effectiveRenderDistance, 4);
        SpaceUpdateStarGlow(player.position);
        SpaceUpdateSolarGlow(player.position);
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
            if (brokenType != BLOCK_AIR) {
                ParticlesEmitBurst((Vector3){ hit.x + 0.5f, hit.y + 0.5f, hit.z + 0.5f },
                                   BlockBaseColor(brokenType), 16, 3.0f, 0.7f);
                AudioPlayBreak();
            }
            SetBlock(hit.x, hit.y, hit.z, BLOCK_AIR);
        }
        int placeX = 0;
        int placeY = 0;
        int placeZ = 0;
        bool canPlace = false;
        if (!inputBlocked && hit.hit) {
            placeX = hit.x + hit.nx;
            placeY = hit.y + hit.ny;
            placeZ = hit.z + hit.nz;
            canPlace = GetBlockAt(placeX, placeY, placeZ) == BLOCK_AIR &&
                       (placeY >= SPACE_LAYER_Y || InHeight(placeY) ||
                        (placeY >= NETHER_LAYER_Y && placeY < 0)) &&
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
                ParticlesEmitBurst((Vector3){ placeX + 0.5f, placeY + 0.5f, placeZ + 0.5f },
                                   BlockBaseColor(placedType), 8, 2.0f, 0.5f);
                AudioPlayPlace();
                SetBlock(placeX, placeY, placeZ, placedType);
            }
        }
        if (!inputBlocked && hit.hit && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
            BlockType picked = GetBlockAt(hit.x, hit.y, hit.z);
            if (picked != BLOCK_AIR && IsValidBlockType(picked)) {
                hotbar[selectedIndex] = picked;
                AudioPlayPick();
                SetImportMessage(TextFormat("Picked %s", BlockName(picked)));
            }
        }

        float daylight = 0.0f;
        float sunset = 0.0f;
        DayNightFactors(dayTime, &daylight, &sunset);
        if (!paused && !albumOpen && !importDialog.open) {
            EntitiesUpdate(dt, &player, daylight);
        }
        float spaceFade = Clamp((camera.position.y - SPACE_EXIT_Y) / (SPACE_ENTER_Y - SPACE_EXIT_Y), 0.0f, 1.0f);
        Color skyTop = { 0 };
        Color skyHorizon = { 0 };
        SkyColorsForLight(daylight, sunset, &skyTop, &skyHorizon);
        Color worldTint = MixWeather(WorldTintForLight(daylight, sunset), daylight);
        skyTop = MixWeather(skyTop, daylight);
        skyHorizon = MixWeather(skyHorizon, daylight);
        skyTop = ColorLerp(skyTop, BLACK, spaceFade);
        skyHorizon = ColorLerp(skyHorizon, BLACK, spaceFade);
        worldTint = ColorLerp(worldTint, (Color){ 46, 54, 78, 255 }, spaceFade);
        bool inNether = camera.position.y < 0.0f;
        if (inNether) {
            skyTop = (Color){ 24, 6, 6, 255 };
            skyHorizon = (Color){ 40, 10, 8, 255 };
            worldTint = (Color){ 150, 62, 42, 255 };
            spaceFade = 0.0f;
        }

        BeginDrawing();
        ClearBackground(skyTop);
        DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), skyTop, skyHorizon);

        BeginMode3D(camera);
        DrawWorld(&camera, effectiveRenderDistance, worldTint);
        EntitiesDraw();
        if (spaceFade < 0.5f && !inNether) DrawClouds(&camera, Fade(worldTint, 1.0f - spaceFade * 2.0f));
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
        if (spaceFade > 0.05f && haveAimBody && !StarMapIsOpen()) {
            DrawBodyInfoPanel(&aimBody);
        }
        if (spaceFade < 0.5f && !inNether) DrawCelestial(&camera, dayTime, daylight);
        DrawCrosshair(GetScreenWidth(), GetScreenHeight());
        DrawHotbar(hotbar, selectedIndex);
        DrawImportStatus();
        int hour = (int)(dayTime * 24.0f) % 24;
        DrawText(TextFormat("XYZ %d %d %d    %02d:00", (int)floorf(player.position.x),
                            (int)floorf(player.position.y), (int)floorf(player.position.z), hour),
                 14, GetScreenHeight() - 34, 16, Fade(WHITE, 0.75f));
        DrawText(TextFormat("Auto-save: %s", autoSaveEnabled ? "60s" : "off"),
                 14, GetScreenHeight() - 16, 14, Fade(WHITE, 0.45f));
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
            if (saveWorld) SaveMap(&player);
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
                SaveMap(&player);
                quitRequested = true;
            }
        }

        EndDrawing();
    }

    if (screen == SCREEN_PLAYING) {
        if (ShipIsDriving()) ShipExit(&player);
        SaveMap(&player);
    }
    ChunksShutdownGenThread();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
    UnloadAllNetherChunks();
    UnloadTexture(blockAtlas);
    if (cloudModel.meshCount > 0) UnloadModel(cloudModel);
    AudioShutdown();
    CloseWindow();
    AlbumCleanup();
    WorldCleanup();
    return 0;
}

