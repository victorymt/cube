#ifndef VOXELCRAFT_INTERACTION_H
#define VOXELCRAFT_INTERACTION_H

#include "types.h"

typedef struct ImageImportPlan {
    int targetWidth;
    int targetHeight;
    uint64_t sourcePixels;
    uint64_t targetPixels;
    uint64_t maximumBlockOperations;
} ImageImportPlan;

void AdjustRenderDistance(int delta);
int ClampImportPrecision(int value);
int AdjustImportPrecision(int value, int delta);
bool BuildImageImportPlan(int imageWidth, int imageHeight, int maxBlocks,
                          bool relief, ImageImportPlan *outPlan);
bool IsSupportedImageFile(const char *path);
HitResult RaycastBlocks(Vector3 origin, Vector3 direction, float maxDistance);
float RaycastCameraOcclusion(Vector3 origin, Vector3 direction, float maxDistance);
bool BlockWouldOverlapPlayer(int x, int y, int z, Vector3 playerPosition);

void OpenImportDialog(ImportDialog *dialog);
void UpdateImportDialog(ImportDialog *dialog, const Player *player, bool *cursorReleased);
void HandleImageDrop(const Player *player, int maxBlocks, bool relief);
void ImportImageAsBlocks(const char *path, const Player *player, int maxBlocks, bool relief);

#endif
