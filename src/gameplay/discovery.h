#ifndef VOXELCRAFT_DISCOVERY_H
#define VOXELCRAFT_DISCOVERY_H

#include "world/world_types.h"

typedef enum PlanetPoiType {
    PLANET_POI_RELIC = 0,
    PLANET_POI_RESOURCE_CACHE,
    PLANET_POI_ANOMALY
} PlanetPoiType;

typedef struct PlanetPoi {
    PlanetPoiType type;
    int x;
    int y;
    int z;
    BlockType coreBlock;
    BlockType rewardBlock;
    int rewardAmount;
    char name[32];
} PlanetPoi;

void PlanetPoiApplyToChunk(Chunk *chunk, int chunkX, int chunkZ);
bool PlanetPoiNearest(Vector3 playerPosition, PlanetPoi *out);
bool PlanetPoiIsCore(int x, int y, int z);
bool PlanetPoiIsClaimed(int x, int y, int z);
bool PlanetPoiTryClaim(int x, int y, int z, PlanetPoi *out);
void PlanetPoiDrawScanner(const Camera3D *camera, Vector3 playerPosition);

#endif
