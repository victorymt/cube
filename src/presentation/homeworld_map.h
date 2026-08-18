#ifndef VOXELCRAFT_HOMEWORLD_MAP_H
#define VOXELCRAFT_HOMEWORLD_MAP_H

#include "world/world_types.h"

void HomeWorldMapOpen(Vector3 playerPosition, float daylight);
void HomeWorldMapClose(void);
bool HomeWorldMapIsOpen(void);
void HomeWorldMapSetSubsurfaceLiquidsVisible(bool visible);
bool HomeWorldMapSubsurfaceLiquidsVisible(void);
void HomeWorldMapUpdate(Vector3 playerPosition, float playerYaw,
                        float daylight);
void HomeWorldMapDraw(void);
void HomeWorldMapUnload(void);

#endif
