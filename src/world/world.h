#ifndef VOXELCRAFT_WORLD_H
#define VOXELCRAFT_WORLD_H

#include "world/world_types.h"
#include "gameplay/player_types.h"

#include <stdint.h>

uint32_t WorldGetSeed(void);
void WorldSetSeed(uint32_t seed);
void WorldReset(uint32_t seed);
TerrainMode WorldTerrainMode(void);
void WorldSetTerrainMode(TerrainMode mode);
void WorldNotifyChunkLoaded(Chunk *chunk);
void WorldNotifyChunkSectionLoaded(Chunk *chunk, int sectionY);
bool WorldPrepareChunkSectionUnload(Chunk *chunk, int sectionY);

const char *BlockName(BlockType type);
bool IsColorBlock(BlockType type);
bool IsValidBlockType(BlockType type);
bool IsWaterBlock(BlockType type);
bool IsLiquidBlock(BlockType type);
float BlockCollisionHeight(BlockType type);
float BlockCollisionHeightAt(int x, int y, int z);
bool IsTranslucentBlock(BlockType type);
int ColorBlockIndex(BlockType type);
BlockType ColorBlockFromIndex(int index);
Color ColorPalette256(int index);
Color BlockBaseColor(BlockType type);
BlockType NearestImageBlock(Color color);

void TorchLightAdd(int x, int y, int z);
void TorchLightRemove(int x, int y, int z);
void RebuildTorchList(void);
float TorchLightAtBlockNearby(int x, int y, int z, const int *indices, int count);
int CollectNearbyTorchLights(int chunkMinX, int chunkMaxX, int chunkMinZ, int chunkMaxZ, int *indices);
int WorldGetEditCount(void);
uint64_t WorldGetEditRevision(void);
const BlockEdit *WorldGetEditAt(int index);
uint32_t WorldGetEditDimensionAt(int index);
bool WorldGetEditSurfaceAddressAt(int index, SurfaceAddress *outAddress);
bool WorldGetEditForCurrentDimension(int index, BlockEdit *outEdit);
bool WorldGetBlockEditForCurrentDimensionAt(int x, int y, int z,
                                            BlockType *outType);

void SetImportMessage(const char *message);
const char *WorldGetImportMessage(void);
float WorldGetImportMessageTimer(void);
void WorldTickImportMessage(float dt);

void WorldBeginUndoGroup(void);
void WorldEndUndoGroup(void);
BlockType GetBlockAt(int x, int y, int z);
bool SetBlock(int x, int y, int z, BlockType type);
bool SetBlockNoUndo(int x, int y, int z, BlockType type);
bool SetBlockForImport(int x, int y, int z, BlockType type);
bool UndoBlockEdit(void);
bool RedoBlockEdit(void);
void ClearUndoHistory(void);

void SaveMap(const Player *player);
void LoadMap(Player *player);
void WorldCleanup(void);

#endif
