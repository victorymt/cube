#ifndef VOXELCRAFT_FLUID_TEST_H
#define VOXELCRAFT_FLUID_TEST_H

#include "world/fluid.h"

#ifdef VOXELCRAFT_FLUID_TESTING
bool FluidTestAddressesShareChunk(SurfaceAddress a, SurfaceAddress b,
                                  bool *outShare);
#endif

#endif
