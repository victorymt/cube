#ifndef VOXELCRAFT_TERRAIN_TEST_H
#define VOXELCRAFT_TERRAIN_TEST_H

/* Test-only generation seams.
 *
 * These used to sit inside `terrain.h` behind `#ifdef TERRAIN_TESTING`, which
 * meant the production header carried eleven declarations that only test
 * builds ever compiled. They live here now, still guarded, so the production
 * header states only what production can call. */

#include "space/planet_profile.h"
#include "world/block_type.h"
#include "world/chunk_types.h"
#include "space/planet_surface.h"
#include "world/terrain_types.h"

#ifdef TERRAIN_TESTING
BlockType TerrainTestPlanetSubsurfaceBlock(SolarBodyStyle style,
                                           PlanetBiome biome, int depth,
                                           unsigned int hash);
void TerrainTestBootstrapHomeChunk(Chunk *chunk, int cx, int cz,
                                   TerrainMode mode);
void TerrainTestResolveMaterializedSections(Chunk *chunk);
void TerrainTestGenerateMineshaft(Chunk *chunk, int cx, int cz,
                                  TerrainMode mode);
void TerrainTestGenerateStructures(Chunk *chunk, int cx, int cz,
                                   TerrainMode mode);
int TerrainTestHomeTreeVariantAt(int treeX, int treeZ, bool conifer);
int TerrainTestHomeTreeCrownRadiusAt(int treeX, int treeZ);
void TerrainTestPlaceHomeTree(Chunk *chunk, int treeX, int base, int treeZ,
                              bool conifer, int variant);
int TerrainTestHomeTreeTaxonAt(int treeX, int treeZ);
void TerrainTestPlaceHomeTreeTaxon(Chunk *chunk, int treeX, int base,
                                   int treeZ, int taxonId);
BlockType TerrainTestHomeGroundCoverBlock(
    Biome biome, int height, int seaLevel, unsigned int hash);
#endif /* TERRAIN_TESTING */

#endif /* VOXELCRAFT_TERRAIN_TEST_H */
