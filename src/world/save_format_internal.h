#ifndef VOXELCRAFT_SAVE_FORMAT_INTERNAL_H
#define VOXELCRAFT_SAVE_FORMAT_INTERNAL_H

#include <stdbool.h>
#include <stdio.h>

typedef enum WorldSaveFormat {
    WORLD_SAVE_FORMAT_UNSUPPORTED = 0,
    WORLD_SAVE_FORMAT_V18,
    WORLD_SAVE_FORMAT_V19
} WorldSaveFormat;

WorldSaveFormat WorldSaveFormatRead(FILE *file);
bool WorldSaveFormatWriteCurrent(FILE *file);
bool WorldSaveFormatHasMapMarkers(WorldSaveFormat format);

#endif
