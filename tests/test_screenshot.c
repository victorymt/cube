#include "screenshot.h"

#include "raylib.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static size_t ReadFile(const char *path, char *buffer, size_t bufferSize)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    size_t length = fread(buffer, 1, bufferSize - 1, file);
    assert(!ferror(file));
    buffer[length] = '\0';
    fclose(file);
    return length;
}

static void TestInvalidArguments(void)
{
    char path[256];
    assert(ScreenshotNextPath(NULL, time(NULL), path, sizeof(path)) ==
           SCREENSHOT_RESULT_INVALID_ARGUMENT);
    assert(ScreenshotNextPath("/tmp", time(NULL), NULL, 0) ==
           SCREENSHOT_RESULT_INVALID_ARGUMENT);
    char tiny[8];
    assert(ScreenshotNextPath("/tmp", time(NULL), tiny, sizeof(tiny)) ==
           SCREENSHOT_RESULT_INVALID_ARGUMENT);

    char reportPath[256];
    assert(ScreenshotDebugReportPath(NULL, reportPath, sizeof(reportPath)) ==
           SCREENSHOT_RESULT_INVALID_ARGUMENT);
    assert(ScreenshotDebugReportPath("capture.jpg", reportPath,
                                     sizeof(reportPath)) ==
           SCREENSHOT_RESULT_INVALID_ARGUMENT);
    assert(ScreenshotDebugReportPath("capture.png", tiny, sizeof(tiny)) ==
           SCREENSHOT_RESULT_INVALID_ARGUMENT);
    assert(ScreenshotWriteDebugReport("capture.png", time(NULL), NULL,
                                      reportPath, sizeof(reportPath)) ==
           SCREENSHOT_RESULT_INVALID_ARGUMENT);
}

static void TestDirectoryCreationAndCollisionAvoidance(void)
{
    char directory[160];
    snprintf(directory, sizeof(directory),
             "/tmp/voxelcraft_screenshot_test_%ld", (long)getpid());
    if (DirectoryExists(directory)) rmdir(directory);

    time_t timestamp = time(NULL);
    char first[512];
    assert(ScreenshotNextPath(directory, timestamp, first, sizeof(first)) ==
           SCREENSHOT_RESULT_OK);
    assert(DirectoryExists(directory));
    assert(strncmp(first, directory, strlen(directory)) == 0);
    assert(strstr(first, "/voxelcraft_") != NULL);
    assert(strstr(first, "_000.png") != NULL);

    char orphanReport[512];
    assert(ScreenshotDebugReportPath(first, orphanReport,
                                     sizeof(orphanReport)) ==
           SCREENSHOT_RESULT_OK);
    FILE *file = fopen(orphanReport, "wb");
    assert(file);
    fclose(file);

    char second[512];
    assert(ScreenshotNextPath(directory, timestamp, second, sizeof(second)) ==
           SCREENSHOT_RESULT_OK);
    assert(strcmp(first, second) != 0);
    assert(strstr(second, "_001.png") != NULL);

    file = fopen(second, "wb");
    assert(file);
    fclose(file);
    char third[512];
    assert(ScreenshotNextPath(directory, timestamp, third, sizeof(third)) ==
           SCREENSHOT_RESULT_OK);
    assert(strstr(third, "_002.png") != NULL);

    unlink(orphanReport);
    unlink(second);
    rmdir(directory);
}

static void TestDebugReport(void)
{
    char directory[160];
    snprintf(directory, sizeof(directory),
             "/tmp/voxelcraft_screenshot_report_%ld", (long)getpid());
    if (!DirectoryExists(directory)) assert(MakeDirectory(directory) == 0);

    char imagePath[256];
    snprintf(imagePath, sizeof(imagePath), "%s/capture.png", directory);
    ScreenshotDebugInfo info = {
        .world = {
            .seed = 424242u,
            .surfaceId = 17u,
            .dimension = "planet",
            .dayTime = 0.625f,
            .daylight = 0.75f,
            .dayCycleEnabled = true
        },
        .player = {
            .position = { 1.25f, 42.5f, -8.75f },
            .velocity = { 0.5f, -1.0f, 2.0f },
            .yaw = 1.5f,
            .pitch = -0.25f,
            .onGround = false,
            .floating = true,
            .driving = false
        },
        .camera = {
            .position = { 2.0f, 43.0f, -7.0f },
            .target = { 3.0f, 43.5f, -6.0f },
            .fovY = 70.0f,
            .thirdPerson = true,
            .insideSolid = false
        },
        .weather = {
            .name = "Rain",
            .simulationTime = 1234.5,
            .active = true,
            .atmosphereDensity = 0.9f,
            .cloudCover = 0.8125f,
            .cloudBaseHeight = 24.0f,
            .cloudThickness = 18.0f,
            .cloudOpacity = 0.7f,
            .fogDensity = 0.1f,
            .visibility = 0.65f,
            .precipitationVeil = 0.5f,
            .stormDarkening = 0.3f,
            .windDrift = 5.0f,
            .windAngle = 2.25f,
            .snowFraction = 0.0f
        },
        .environment = {
            .altitude = 31.0f,
            .atmosphereFade = 0.0f,
            .seabedY = 18,
            .waterColumnDepth = 62,
            .bathymetryZone = "trench",
            .seabedMaterial = "rock",
            .sheltered = true
        },
        .render = {
            .graphicsQuality = "high",
            .renderDistanceChunks = 12,
            .fps = 59,
            .screenWidth = 1280,
            .screenHeight = 720,
            .frameTimeMs = 16.667f,
            .performanceMode = true
        },
        .ui = {
            .debugHudVisible = true
        },
        .streaming = {
            .activeChunks = 81,
            .activeEntities = 7,
            .surfaceChunkX = -181,
            .surfaceChunkZ = 1,
            .surfaceSectionY = 4,
            .surfaceChunkLoaded = true,
            .waterNeighborLoadedMask = 0xFu,
            .waterTriangleCount = 972,
            .waterSectionTriangleCount = 144,
            .generationSubmitted = 123u,
            .meshCompleted = 456u,
            .meshCpuMs = 78.25
        },
        .evolution = {
            .entitySelected = true,
            .scanLocked = true,
            .atlasOpen = true,
            .organismId = 71u,
            .lineageId = 72u,
            .speciesId = 73u,
            .genomeId = 74u,
            .generation = 4u,
            .mutationCount = 3u,
            .moduleCount = 12u,
            .motherId = 69u,
            .fatherId = 70u,
            .childCount = 2u,
            .catalogSpeciesCount = 5u,
            .catalogIndividualCount = 9u,
            .regionalLineageCount = 3u,
            .bootstrapGeneration = 24u,
            .bootstrapComplete = true,
            .regionAvailable = true,
            .sex = "female",
            .locomotion = "flight",
            .ageDays = 22.0f,
            .maturityAgeDays = 18.0f,
            .health = 0.9f,
            .energy = 0.8f,
            .diet = 0.6f,
            .mass = 2.4f,
            .speed = 1.7f,
            .herbivoreDensity = 0.2f,
            .omnivoreDensity = 0.1f,
            .carnivoreDensity = 0.05f
        }
    };

    char reportPath[256];
    time_t timestamp = (time_t)1700000000;
    assert(ScreenshotWriteDebugReport(imagePath, timestamp, &info,
                                      reportPath, sizeof(reportPath)) ==
           SCREENSHOT_RESULT_OK);
    assert(strstr(reportPath, "/capture.txt") != NULL);

    char contents[8192];
    assert(ReadFile(reportPath, contents, sizeof(contents)) > 0);
    assert(strstr(contents, "format.version=4\n"));
    assert(strstr(contents, "capture.unix_time=1700000000\n"));
    assert(strstr(contents, "world.seed=424242\n"));
    assert(strstr(contents, "world.dimension=planet\n"));
    assert(strstr(contents, "player.position=1.250000,42.500000,-8.750000\n"));
    assert(strstr(contents, "camera.inside_solid=false\n"));
    assert(strstr(contents, "weather.cloud_cover=0.812500\n"));
    assert(strstr(contents, "weather.cloud_thickness=18.000000\n"));
    assert(strstr(contents, "environment.seabed_y=18\n"));
    assert(strstr(contents, "environment.water_column_depth=62\n"));
    assert(strstr(contents, "environment.bathymetry_zone=trench\n"));
    assert(strstr(contents, "environment.seabed_material=rock\n"));
    assert(strstr(contents, "render.screen=1280,720\n"));
    assert(strstr(contents, "render.performance_mode=true\n"));
    assert(strstr(contents, "ui.debug_hud_visible=true\n"));
    assert(strstr(contents, "streaming.active_chunks=81\n"));
    assert(strstr(contents, "streaming.surface_chunk=-181,1\n"));
    assert(strstr(contents, "streaming.water_neighbor_loaded_mask=0xF\n"));
    assert(strstr(contents, "streaming.water_triangle_count=972\n"));
    assert(strstr(contents, "streaming.water_section_triangle_count=144\n"));
    assert(strstr(contents, "streaming.mesh_completed=456\n"));
    assert(strstr(contents, "evolution.organism_id=71\n"));
    assert(strstr(contents, "evolution.scan_locked=true\n"));
    assert(strstr(contents, "evolution.atlas_open=true\n"));
    assert(strstr(contents, "evolution.child_count=2\n"));
    assert(strstr(contents, "evolution.catalog_species_count=5\n"));
    assert(strstr(contents, "evolution.locomotion=flight\n"));
    assert(strstr(contents, "evolution.bootstrap_complete=true\n"));

    unlink(reportPath);
    rmdir(directory);
}

static void TestDebugReportFailure(void)
{
    char filePath[160];
    snprintf(filePath, sizeof(filePath),
             "/tmp/voxelcraft_screenshot_report_file_%ld", (long)getpid());
    FILE *file = fopen(filePath, "wb");
    assert(file);
    fclose(file);

    char imagePath[256];
    snprintf(imagePath, sizeof(imagePath), "%s/capture.png", filePath);
    char reportPath[256];
    ScreenshotDebugInfo info = { 0 };
    assert(ScreenshotWriteDebugReport(imagePath, time(NULL), &info,
                                      reportPath, sizeof(reportPath)) ==
           SCREENSHOT_RESULT_REPORT_WRITE_FAILED);
    unlink(filePath);
}

static void TestDirectoryFailure(void)
{
    char filePath[160];
    snprintf(filePath, sizeof(filePath),
             "/tmp/voxelcraft_screenshot_file_%ld", (long)getpid());
    FILE *file = fopen(filePath, "wb");
    assert(file);
    fclose(file);

    char path[512];
    assert(ScreenshotNextPath(filePath, time(NULL), path, sizeof(path)) ==
           SCREENSHOT_RESULT_DIRECTORY_FAILED);
    unlink(filePath);
}

static void TestResultMessages(void)
{
    assert(strstr(ScreenshotResultMessage(SCREENSHOT_RESULT_OK), "saved"));
    assert(strstr(ScreenshotResultMessage(SCREENSHOT_RESULT_DIRECTORY_FAILED),
                  "directory"));
    assert(strstr(ScreenshotResultMessage(SCREENSHOT_RESULT_WRITE_FAILED),
                  "written"));
    assert(strstr(ScreenshotResultMessage(
                      SCREENSHOT_RESULT_REPORT_WRITE_FAILED),
                  "report"));
}

int main(void)
{
    TestInvalidArguments();
    TestDirectoryCreationAndCollisionAvoidance();
    TestDirectoryFailure();
    TestDebugReport();
    TestDebugReportFailure();
    TestResultMessages();
    puts("screenshot tests passed");
    return 0;
}
