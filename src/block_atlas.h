#ifndef VOXELCRAFT_BLOCK_ATLAS_H
#define VOXELCRAFT_BLOCK_ATLAS_H

#include "types.h"

void DrawAtlasTile(Image *image, BlockTexture texture);
Texture2D LoadBlockAtlas(void);
BlockTexture TextureForBlockFace(BlockType type, int face);
void AtlasUVs(BlockTexture texture, Vector2 uvs[6]);
Rectangle AtlasSourceRect(BlockTexture texture);

#endif
