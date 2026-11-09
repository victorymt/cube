#ifndef VOXELCRAFT_WORLD_SURFACE_CONTRACT_FACTORY_H
#define VOXELCRAFT_WORLD_SURFACE_CONTRACT_FACTORY_H

#include "world/surface_contract.h"
#include "world/terrain_types.h"
#include "world/world_surface_snapshot.h"

#include <stdint.h>

/* Creates the one active backend for an installed surface snapshot. The cube
 * implementation owns an immutable TerrainCubeContext for its whole lifetime;
 * the legacy implementation owns only copied sampling inputs. */
SurfaceContract *SurfaceContractCreate(const WorldSurfaceSnapshot *snapshot,
                                       TerrainMode mode, uint64_t generation);

#endif
