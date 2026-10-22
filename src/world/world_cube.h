#ifndef VOXELCRAFT_WORLD_CUBE_H
#define VOXELCRAFT_WORLD_CUBE_H

#include "world/world_types.h"
#include "world/world_location.h"
#include "world/surface_pose.h"

#define WORLD_CUBE_MAX_EDITS (1u << 20)

// Main-thread cube edit store. Addresses never pass through map coordinates.
// Home uses body 0; planetary bodies are nonzero. Space/Nether use their own
// Cartesian stores. Disk persistence/undo are not yet connected to this API.
bool WorldCubeSetBlock(WorldBlockAddress address, BlockType type);
// Returns false for unknown/unresolved terrain; never invents air there.
bool WorldCubeBlockAt(WorldBlockAddress address, BlockType *outType);
bool WorldCubeGetEdit(WorldBlockAddress address, BlockType *outType);
unsigned WorldCubeEditCount(void);
bool WorldCubeEditAt(unsigned index, WorldBlockAddress *outAddress, BlockType *outType);
void WorldCubeClearEdits(void);
#ifdef CHUNKS_TESTING
void WorldCubeTestFailNextReserve(void);
#endif

// Main-thread generation completion, before publishing results. Column replay
// touches only resolved sections; edits to deep/unresolved sections remain in
// the store until those sections are generated.
/* Block at a continuous body-centred position. Returns false when the owning
 * section is not generated, so callers never read ungenerated terrain as air. */
bool WorldCubeBlockAtPoint(uint32_t bodyId, SurfaceVector point,
                            BlockType *outType);

bool WorldCubeApplyEdits(Chunk *staging);
void WorldCubeApplySectionEdits(SurfaceCubeChunkKey key, int sectionY,
    unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE]);

#endif
