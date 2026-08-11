#ifndef VOXELCRAFT_SPACE_QUERY_CACHE_H
#define VOXELCRAFT_SPACE_QUERY_CACHE_H

#include "space.h"

#include <stdbool.h>
#include <stdint.h>

bool SpaceQueryDefinitionCacheGet(uint32_t worldSeed, int anchorX,
                                  int anchorZ, SolarSystemDef *out);
void SpaceQueryDefinitionCachePut(uint32_t worldSeed, int anchorX,
                                  int anchorZ, const SolarSystemDef *value);
bool SpaceQueryRuntimeCacheGet(uint32_t worldSeed, int anchorX, int anchorZ,
                               uint64_t systemSignature, double simulationTime,
                               SolarSystemRuntimeState *out);
void SpaceQueryRuntimeCachePut(uint32_t worldSeed, int anchorX, int anchorZ,
                               uint64_t systemSignature, double simulationTime,
                               const SolarSystemRuntimeState *value);

#endif
