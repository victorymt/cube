#ifndef VOXELCRAFT_BLOCK_ATLAS_ARTWORK_INTERNAL_H
#define VOXELCRAFT_BLOCK_ATLAS_ARTWORK_INTERNAL_H

#include "world/block_atlas.h"

Color AtlasColorWithNoise(Color base, int amount, unsigned int hash);
Color GeologyAtlasPixel(BlockTexture texture, int x, int y,
                         unsigned int hash);
Color EcologyAtlasPixel(BlockTexture texture, int x, int y,
                         unsigned int hash);
Color ItemAtlasPixel(BlockTexture texture, int x, int y,
                      unsigned int hash);
Color SpaceAtlasPixel(BlockTexture texture, int x, int y,
                       unsigned int hash);

#endif
