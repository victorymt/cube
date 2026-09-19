#ifndef VOXELCRAFT_SAVE_FORMAT_H
#define VOXELCRAFT_SAVE_FORMAT_H

#include <stdbool.h>
#include <stdio.h>

typedef enum WorldSaveFormat {
    WORLD_SAVE_FORMAT_UNSUPPORTED = 0,
    WORLD_SAVE_FORMAT_V18,
    WORLD_SAVE_FORMAT_V19,
    WORLD_SAVE_FORMAT_V20,
    WORLD_SAVE_FORMAT_V21,
    WORLD_SAVE_FORMAT_V22,
    WORLD_SAVE_FORMAT_V23
} WorldSaveFormat;

WorldSaveFormat WorldSaveFormatRead(FILE *file);
bool WorldSaveFormatWriteCurrent(FILE *file);
bool WorldSaveFormatHasMapMarkers(WorldSaveFormat format);
bool WorldSaveFormatHasWeatherImpact(WorldSaveFormat format);
bool WorldSaveFormatHasRadiationRuntime(WorldSaveFormat format);
bool WorldSaveFormatHasHydrologyRuntime(WorldSaveFormat format);
bool WorldSaveFormatHasEcologyClock(WorldSaveFormat format);

#endif
