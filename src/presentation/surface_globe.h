#ifndef VOXELCRAFT_SURFACE_GLOBE_H
#define VOXELCRAFT_SURFACE_GLOBE_H

#include "raylib.h"

#include <stdbool.h>

typedef struct SurfaceGlobeDrawParams {
    Rectangle destination;
    bool planetSurface;
    float cameraLongitude;
    float cameraLatitude;
    float markerLongitude;
    float markerLatitude;
} SurfaceGlobeDrawParams;

bool SurfaceGlobeDraw(const SurfaceGlobeDrawParams *params);
void SurfaceGlobeUnload(void);

#endif
