#ifndef VOXELCRAFT_TERRAIN_CUBE_FLORA_H
#define VOXELCRAFT_TERRAIN_CUBE_FLORA_H

#include "world/chunk_types.h"
#include "world/flora_taxa.h"
#include "world/home_tree_shape.h"
#include "world/terrain_cube.h"

typedef struct TerrainCubeHomeTree {
    SurfaceAddress root;
    int baseY;
    FloraTaxonId taxonId;
    uint32_t shapeHash;
    HomeTreeShapeBounds bounds;
} TerrainCubeHomeTree;

// Resolves the complete deterministic home-tree decision for one canopy root.
// The root's radial component is ignored; rootAddress.radial in the result is
// the first trunk block. This performs biome, soil, density and spacing checks.
bool TerrainCubeHomeTreeAt(const TerrainCubeContext *context,
                           SurfaceAddress root, TerrainCubeHomeTree *outTree);
bool TerrainCubeGenerateHomeTrees(const TerrainCubeContext *context,
                                  Chunk *staging, int sectionY);

#endif
