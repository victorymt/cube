#ifndef VOXELCRAFT_MAP_MARKERS_H
#define VOXELCRAFT_MAP_MARKERS_H

#include "raylib.h"
#include "world/world_environment.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MAP_MARKER_NAME_SIZE 64
#define MAP_MARKERS_PER_SURFACE 64
#define MAP_MARKERS_TOTAL 1024
#define MAP_MARKER_COORDINATE_SCHEMA 2u

typedef enum MapMarkerColor {
    MAP_MARKER_RED = 0,
    MAP_MARKER_AMBER,
    MAP_MARKER_GREEN,
    MAP_MARKER_CYAN,
    MAP_MARKER_BLUE,
    MAP_MARKER_MAGENTA,
    MAP_MARKER_COLOR_COUNT
} MapMarkerColor;

typedef struct MapMarkerSurface {
    WorldDimension dimension;
    uint32_t surfaceId;
} MapMarkerSurface;

typedef struct MapMarker {
    uint32_t id;
    MapMarkerSurface surface;
    float x;
    float z;
    MapMarkerColor color;
    char name[MAP_MARKER_NAME_SIZE];
} MapMarker;

typedef struct MapMarkerState {
    MapMarker markers[MAP_MARKERS_TOTAL];
    uint32_t count;
    uint32_t nextId;
    uint32_t targetId;
    uint32_t coordinateSchema;
} MapMarkerState;

void MapMarkersReset(void);
void MapMarkersEmptyState(MapMarkerState *state);
bool MapMarkersReadState(FILE *file, MapMarkerState *out);
bool MapMarkersMigrateLegacyState(MapMarkerState *state,
                                  uint32_t currentPlanetId,
                                  int currentPlanetOriginX,
                                  int currentPlanetOriginZ);
bool MapMarkersSaveState(FILE *file);
bool MapMarkersInstallState(const MapMarkerState *state);

int MapMarkersCount(MapMarkerSurface surface);
int MapMarkersCollect(MapMarkerSurface surface, MapMarker *out, int capacity);
bool MapMarkersFind(uint32_t id, MapMarker *out);
bool MapMarkersCreate(MapMarkerSurface surface, float x, float z,
                      const char *name, MapMarkerColor color,
                      uint32_t *outId);
bool MapMarkersUpdate(uint32_t id, const char *name, MapMarkerColor color);
bool MapMarkersRemove(uint32_t id);

bool MapMarkersToggleTarget(uint32_t id);
bool MapMarkersSetTarget(uint32_t id);
bool MapMarkersTarget(MapMarker *out);
bool MapMarkersTargetOnSurface(MapMarkerSurface surface, MapMarker *out);
uint32_t MapMarkersTargetId(void);

Color MapMarkerColorValue(MapMarkerColor color);
bool MapMarkerNameIsValid(const char *name);
bool MapMarkerNameAppendCodepoint(char *name, size_t capacity, int codepoint);
bool MapMarkerNameAppendUtf8(char *name, size_t capacity, const char *text);
void MapMarkerNameBackspace(char *name);

bool MapMarkerGreatCircle(float fromLongitude, float fromLatitude,
                          float toLongitude, float toLatitude,
                          float *outBearing, float *outDistance);

#ifdef MAP_MARKERS_TESTING
void MapMarkersTestSetCoordinateSchema(uint32_t schema);
#endif

#endif
