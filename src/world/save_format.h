#ifndef VOXELCRAFT_SAVE_FORMAT_H
#define VOXELCRAFT_SAVE_FORMAT_H

#include "world/surface_topology_version.h"

#include <stdbool.h>
#include <stdio.h>

typedef enum WorldSaveFormat {
    WORLD_SAVE_FORMAT_UNSUPPORTED = 0,
    WORLD_SAVE_FORMAT_V18,
    WORLD_SAVE_FORMAT_V19,
    WORLD_SAVE_FORMAT_V20,
    WORLD_SAVE_FORMAT_V21,
    WORLD_SAVE_FORMAT_V22,
    WORLD_SAVE_FORMAT_V23,
    WORLD_SAVE_FORMAT_V24,
    WORLD_SAVE_FORMAT_V25,
    WORLD_SAVE_FORMAT_V26,
    WORLD_SAVE_FORMAT_V27,
    WORLD_SAVE_FORMAT_V28,
    WORLD_SAVE_FORMAT_V29
} WorldSaveFormat;

// Historical enum values describe standalone subsystem fixtures only. The game
// reader accepts V29 exclusively.
WorldSaveFormat WorldSaveFormatRead(FILE *file);
bool WorldSaveWriteHeader(FILE *file, WorldSurfaceTopologyPolicy policy);
// Reads/validates magic + TOPOL001 without touching runtime state. Only assigns
// outPolicy on success; the returned static error is NULL on success.
const char *WorldSaveReadHeader(FILE *file, WorldSurfaceTopologyPolicy *outPolicy);
bool WorldSaveFormatWriteCurrent(FILE *file);
bool WorldSaveFormatHasMapMarkers(WorldSaveFormat format);
bool WorldSaveFormatHasWeatherImpact(WorldSaveFormat format);
bool WorldSaveFormatHasRadiationRuntime(WorldSaveFormat format);
bool WorldSaveFormatHasHydrologyRuntime(WorldSaveFormat format);
bool WorldSaveFormatHasEcologyClock(WorldSaveFormat format);
bool WorldSaveFormatHasVolcanoes(WorldSaveFormat format);
bool WorldSaveFormatHasLava(WorldSaveFormat format);
bool WorldSaveFormatHasAsh(WorldSaveFormat format);
bool WorldSaveFormatHasInventoryRemainders(WorldSaveFormat format);

#endif
