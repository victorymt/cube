#ifndef VOXELCRAFT_WORLD_ECOLOGY_HOOKS_H
#define VOXELCRAFT_WORLD_ECOLOGY_HOOKS_H

#include "world/world_types.h"

#include <stdbool.h>

typedef struct WorldFloraVisualSample {
    float activity;
    float capacity;
    float growthScale;
    float visualPresence;
} WorldFloraVisualSample;

typedef bool (*WorldFloraCapacityProvider)(
    int x, int z, float *outCapacity, void *userData);
typedef bool (*WorldFloraVisualProvider)(
    int x, int z, float daylight, WorldFloraVisualSample *outSample,
    void *userData);
typedef void (*WorldEcologyChunkDecorator)(
    Chunk *chunk, int chunkX, int chunkZ, void *userData);

typedef struct WorldEcologyHooks {
    WorldFloraCapacityProvider floraCapacityAt;
    WorldFloraVisualProvider floraVisualAt;
    WorldEcologyChunkDecorator decorateChunk;
    void *userData;
} WorldEcologyHooks;

/* The application installs immutable hooks before chunk workers start. */
void WorldInstallEcologyHooks(const WorldEcologyHooks *hooks);
bool WorldEcologyFloraCapacityAt(int x, int z, float *outCapacity);
bool WorldEcologyFloraVisualAt(
    int x, int z, float daylight, WorldFloraVisualSample *outSample);
void WorldEcologyDecorateChunk(Chunk *chunk, int chunkX, int chunkZ);

#endif
