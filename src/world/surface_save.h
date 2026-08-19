#ifndef VOXELCRAFT_SURFACE_SAVE_H
#define VOXELCRAFT_SURFACE_SAVE_H

#include "world/surface_topology.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SURFACE_SAVE_SCHEMA_VERSION 2u
#define SURFACE_SAVE_MIN_SCHEMA_VERSION 1u

bool SurfaceSaveWriteTrailer(FILE *file, bool playerHasAddress,
                             SurfaceAddress playerAddress,
                             const SurfaceAddress *editAddresses,
                             const SurfaceMapCell *editMapCells,
                             uint32_t editCount);
bool SurfaceSaveReadTrailer(FILE *file, uint32_t expectedEditCount,
                            uint32_t *outSchemaVersion,
                            bool *outPlayerHasAddress,
                            SurfaceAddress *outPlayerAddress,
                            SurfaceAddress **outEditAddresses,
                            SurfaceMapCell **outEditMapCells);

#endif
