#ifndef VOXELCRAFT_BLOCK_LIGHTING_H
#define VOXELCRAFT_BLOCK_LIGHTING_H

/*
 * Torch emitters and the queries the renderer uses to light nearby faces.
 * Separate from block access because a consumer that only lights geometry does
 * not need to read or write world blocks.
 */

void TorchLightAdd(int x, int y, int z);
void TorchLightRemove(int x, int y, int z);
void RebuildTorchList(void);
float TorchLightAtBlockNearby(int x, int y, int z, const int *indices, int count);
int CollectNearbyTorchLights(int chunkMinX, int chunkMaxX, int chunkMinZ, int chunkMaxZ, int *indices);

#endif
