#ifndef VOXELCRAFT_SCREENSHOT_H
#define VOXELCRAFT_SCREENSHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define SCREENSHOT_DIRECTORY "screenshots"

typedef enum ScreenshotResult {
    SCREENSHOT_RESULT_OK = 0,
    SCREENSHOT_RESULT_INVALID_ARGUMENT,
    SCREENSHOT_RESULT_DIRECTORY_FAILED,
    SCREENSHOT_RESULT_NAME_EXHAUSTED,
    SCREENSHOT_RESULT_WRITE_FAILED,
    SCREENSHOT_RESULT_REPORT_WRITE_FAILED
} ScreenshotResult;

typedef struct ScreenshotVector3 {
    float x;
    float y;
    float z;
} ScreenshotVector3;

typedef struct ScreenshotWorldDebugInfo {
    uint32_t seed;
    uint32_t surfaceId;
    const char *dimension;
    float dayTime;
    float daylight;
    bool dayCycleEnabled;
} ScreenshotWorldDebugInfo;

typedef struct ScreenshotPlayerDebugInfo {
    ScreenshotVector3 position;
    ScreenshotVector3 velocity;
    float yaw;
    float pitch;
    bool onGround;
    bool floating;
    bool driving;
} ScreenshotPlayerDebugInfo;

typedef struct ScreenshotCameraDebugInfo {
    ScreenshotVector3 position;
    ScreenshotVector3 target;
    float fovY;
    bool thirdPerson;
} ScreenshotCameraDebugInfo;

typedef struct ScreenshotWeatherDebugInfo {
    const char *name;
    double simulationTime;
    bool active;
    float atmosphereDensity;
    float cloudCover;
    float cloudBaseHeight;
    float cloudThickness;
    float cloudOpacity;
    float fogDensity;
    float visibility;
    float precipitationVeil;
    float stormDarkening;
    float windDrift;
    float windAngle;
    float snowFraction;
} ScreenshotWeatherDebugInfo;

typedef struct ScreenshotEnvironmentDebugInfo {
    float altitude;
    float atmosphereFade;
    float underwaterDepth;
    float waterSurfaceY;
    bool underwater;
    bool feetSubmerged;
    bool bodySubmerged;
    bool eyesSubmerged;
    bool sheltered;
    bool forest;
    bool nearWater;
    bool shipInterior;
} ScreenshotEnvironmentDebugInfo;

typedef struct ScreenshotInputDebugInfo {
    float forward;
    float strafe;
    float vertical;
    bool sprint;
    unsigned remainingFrames;
} ScreenshotInputDebugInfo;

typedef struct ScreenshotRenderDebugInfo {
    const char *graphicsQuality;
    int renderDistanceChunks;
    int fps;
    int screenWidth;
    int screenHeight;
    float frameTimeMs;
    bool performanceMode;
} ScreenshotRenderDebugInfo;

typedef struct ScreenshotUiDebugInfo {
    bool paused;
    bool albumOpen;
    bool starMapOpen;
    bool importDialogOpen;
    bool cursorReleased;
    bool helpVisible;
    bool debugHudVisible;
    bool landingTransitionActive;
} ScreenshotUiDebugInfo;

typedef struct ScreenshotStreamingDebugInfo {
    int activeChunks;
    int activeSpaceChunks;
    int activeNetherChunks;
    int activeEntities;
    int pendingGenerationJobs;
    int pendingMeshJobs;
    uint64_t generationSubmitted;
    uint64_t generationCompleted;
    uint64_t generationCanceled;
    uint64_t meshSubmitted;
    uint64_t meshCompleted;
    uint64_t meshCanceled;
    uint64_t meshSnapshotBytes;
    uint64_t syncRebuilds;
    uint64_t uploadedMeshes;
    uint64_t uploadBudgetDeferrals;
    uint64_t generationQueuePeak;
    uint64_t meshQueuePeak;
    uint64_t pendingMeshSnapshotBytes;
    uint64_t pendingMeshSnapshotBytesPeak;
    double generationCpuMs;
    double meshCpuMs;
    double uploadCpuMs;
    double maxUploadCpuMs;
} ScreenshotStreamingDebugInfo;

typedef struct ScreenshotDebugInfo {
    ScreenshotWorldDebugInfo world;
    ScreenshotPlayerDebugInfo player;
    ScreenshotCameraDebugInfo camera;
    ScreenshotWeatherDebugInfo weather;
    ScreenshotEnvironmentDebugInfo environment;
    ScreenshotInputDebugInfo input;
    ScreenshotRenderDebugInfo render;
    ScreenshotUiDebugInfo ui;
    ScreenshotStreamingDebugInfo streaming;
} ScreenshotDebugInfo;

ScreenshotResult ScreenshotNextPath(
    const char *directory, time_t timestamp, char *path, size_t pathSize);
ScreenshotResult ScreenshotCaptureFrame(
    const char *directory, time_t timestamp, char *path, size_t pathSize);
ScreenshotResult ScreenshotDebugReportPath(
    const char *imagePath, char *reportPath, size_t reportPathSize);
ScreenshotResult ScreenshotWriteDebugReport(
    const char *imagePath, time_t timestamp, const ScreenshotDebugInfo *info,
    char *reportPath, size_t reportPathSize);
const char *ScreenshotResultMessage(ScreenshotResult result);

#endif
