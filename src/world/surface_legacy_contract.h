#ifndef VOXELCRAFT_WORLD_SURFACE_LEGACY_CONTRACT_H
#define VOXELCRAFT_WORLD_SURFACE_LEGACY_CONTRACT_H

#include "world/surface_contract.h"
#include "world/terrain_types.h"
#include "world/world_surface_snapshot.h"

#include <stdint.h>

SurfaceContract *SurfaceLegacyContractCreate(
    const WorldSurfaceSnapshot *snapshot, TerrainMode mode, uint64_t generation);

#endif
