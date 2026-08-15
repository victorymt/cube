#ifndef VOXELCRAFT_WORLD_EXTENSION_H
#define VOXELCRAFT_WORLD_EXTENSION_H

#include "types.h"

#include <stdint.h>
#include <stdio.h>

#define WORLD_BLOCK_DISPLACEMENT_MAX_CELLS 7

typedef struct FluidVolumeChange {
  int x;
  int y;
  int z;
  uint8_t before;
  uint8_t after;
  uint8_t baseline;
  bool baselineKnown;
} FluidVolumeChange;

typedef struct FluidBlockDisplacement {
  uint8_t count;
  FluidVolumeChange cells[WORLD_BLOCK_DISPLACEMENT_MAX_CELLS];
} FluidBlockDisplacement;

typedef struct WorldExtensionHooks {
  void (*reset)(void);
  void (*cleanup)(void);
  bool (*saveState)(FILE *file);
  bool (*loadState)(FILE *file);
  bool (*tryDisplaceBlock)(int x, int y, int z,
                           FluidBlockDisplacement *outDisplacement);
  bool (*replayBlockDisplacement)(const FluidBlockDisplacement *displacement,
                                  bool after);
  void (*onBlockChanged)(int x, int y, int z, BlockType previous,
                         BlockType next);
  void (*onChunkLoaded)(Chunk *chunk);
} WorldExtensionHooks;

void WorldInstallExtensionHooks(const WorldExtensionHooks *hooks);

#endif
