#ifndef VOXELCRAFT_SURFACE_SAVE_H
#define VOXELCRAFT_SURFACE_SAVE_H

#include "world/surface_topology.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SURFACE_SAVE_SCHEMA_VERSION 1u

bool SurfaceSaveWriteTrailer(FILE *file, bool playerHasAddress,
                             SurfaceAddress playerAddress,
                             const SurfaceAddress *editAddresses,
                             uint32_t editCount);
bool SurfaceSaveReadTrailer(FILE *file, uint32_t expectedEditCount,
                            bool *outPlayerHasAddress,
                            SurfaceAddress *outPlayerAddress,
                            SurfaceAddress **outEditAddresses);

#endif
