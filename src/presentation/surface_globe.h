#ifndef VOXELCRAFT_SURFACE_GLOBE_H
#define VOXELCRAFT_SURFACE_GLOBE_H

#include "raylib.h"

#include <stdbool.h>

typedef struct SurfaceGlobeMarker {
    float longitude;
    float latitude;
    Color color;
    bool selected;
} SurfaceGlobeMarker;

typedef struct SurfaceGlobeDrawParams {
    Rectangle destination;
    bool planetSurface;
    float cameraLongitude;
    float cameraLatitude;
    float markerLongitude;
    float markerLatitude;
    const SurfaceGlobeMarker *markers;
    int markerCount;
} SurfaceGlobeDrawParams;

bool SurfaceGlobeDraw(const SurfaceGlobeDrawParams *params);
bool SurfaceGlobeHitTest(Rectangle destination, float cameraLongitude,
                         float cameraLatitude, Vector2 screen,
                         float *outLongitude, float *outLatitude);
void SurfaceGlobeInvalidate(void);
void SurfaceGlobeUnload(void);

#endif
