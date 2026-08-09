#ifndef VOXELCRAFT_STARMAP_H
#define VOXELCRAFT_STARMAP_H

#include "types.h"
#include "space.h"

void StarMapOpen(void);
bool StarMapIsOpen(void);
void StarMapUpdate(Vector3 playerPosition);
bool StarMapConsumeTravel(SolarSystemDef *outSystem);
void StarMapClose(void);
void StarMapDraw(void);

#endif
