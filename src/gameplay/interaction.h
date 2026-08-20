#ifndef VOXELCRAFT_INTERACTION_H
#define VOXELCRAFT_INTERACTION_H

#include "world/world_types.h"
#include "gameplay/player_types.h"
#include "presentation/ui_types.h"

typedef struct ImageImportPlan {
    int targetWidth;
    int targetHeight;
    uint64_t sourcePixels;
    uint64_t targetPixels;
    uint64_t maximumBlockOperations;
} ImageImportPlan;

typedef enum RaycastBlockMask {
    RAYCAST_BLOCK_SOLID = 1 << 0,
    RAYCAST_BLOCK_LIQUID = 1 << 1,
    RAYCAST_BLOCK_ALL = RAYCAST_BLOCK_SOLID | RAYCAST_BLOCK_LIQUID
} RaycastBlockMask;

void AdjustRenderDistance(int delta);
int ClampImportPrecision(int value);
int AdjustImportPrecision(int value, int delta);
bool BuildImageImportPlan(int imageWidth, int imageHeight, int maxBlocks,
                          bool relief, ImageImportPlan *outPlan);
bool IsSupportedImageFile(const char *path);
HitResult RaycastBlocks(Vector3 origin, Vector3 direction, float maxDistance);
HitResult RaycastBlocksFiltered(Vector3 origin, Vector3 direction,
                                float maxDistance, unsigned mask);
float RaycastCameraOcclusion(Vector3 origin, Vector3 direction, float maxDistance);
bool BlockWouldOverlapPlayer(int x, int y, int z, BlockType blockType,
                             Vector3 playerPosition);

void OpenImportDialog(ImportDialog *dialog);
void UpdateImportDialog(ImportDialog *dialog, const Player *player, bool *cursorReleased);
void HandleImageDrop(const Player *player, int maxBlocks, bool relief);
void ImportImageAsBlocks(const char *path, const Player *player, int maxBlocks, bool relief);

#endif
