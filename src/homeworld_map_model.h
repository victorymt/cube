#ifndef VOXELCRAFT_HOMEWORLD_MAP_MODEL_H
#define VOXELCRAFT_HOMEWORLD_MAP_MODEL_H

#include "types.h"

#define HOMEWORLD_MAP_RASTER_SIZE 64
#define HOMEWORLD_MAP_HEAT_SIZE 16
#define HOMEWORLD_MAP_MAX_LANDMARKS 8
#define HOMEWORLD_MAP_ZOOM_LEVELS 4

typedef struct HomeWorldMapBounds {
    float centerX;
    float centerZ;
    float span;
} HomeWorldMapBounds;

typedef struct HomeWorldMapTerrainCell {
    Biome biome;
    float elevation;
    float seaLevel;
    float slope;
    int waterDepth;
    float faunaActivity;
} HomeWorldMapTerrainCell;

typedef enum HomeWorldMapLandmarkKind {
    HOMEWORLD_MAP_LANDMARK_PEAK = 0,
    HOMEWORLD_MAP_LANDMARK_SHORE,
    HOMEWORLD_MAP_LANDMARK_FOREST,
    HOMEWORLD_MAP_LANDMARK_FAUNA
} HomeWorldMapLandmarkKind;

typedef struct HomeWorldMapLandmark {
    HomeWorldMapLandmarkKind kind;
    int x;
    int z;
    int elevation;
    float score;
} HomeWorldMapLandmark;

float HomeWorldMapSpanForLevel(int level);
Vector2 HomeWorldMapWorldToScreen(HomeWorldMapBounds bounds, Rectangle map,
                                  float worldX, float worldZ);
Vector2 HomeWorldMapScreenToWorld(HomeWorldMapBounds bounds, Rectangle map,
                                  Vector2 screen);
bool HomeWorldMapWorldVisible(HomeWorldMapBounds bounds, float worldX,
                              float worldZ);
Color HomeWorldMapTerrainColor(HomeWorldMapTerrainCell cell);
const char *HomeWorldMapBiomeName(Biome biome, bool water);
float HomeWorldMapHeatSample(const float *heat, float u, float v);
int HomeWorldMapSelectLandmarks(
    const HomeWorldMapTerrainCell *cells, HomeWorldMapBounds bounds,
    HomeWorldMapLandmark *out, int capacity);

#endif
