#ifndef VOXELCRAFT_SHIP_LOCATOR_H
#define VOXELCRAFT_SHIP_LOCATOR_H

#include "world/world_types.h"
#include "world/world_environment.h"

#include <stdio.h>

#define SHIP_LOCATOR_LOCATION_SIZE 48

typedef struct ShipLocatorContext {
    WorldDimension dimension;
    uint32_t surfaceId;
    int spaceOriginX;
    int spaceOriginZ;
} ShipLocatorContext;

typedef struct ShipLocatorRecord {
    bool deployed;
    WorldDimension dimension;
    uint32_t surfaceId;
    int x;
    int y;
    int z;
    char location[SHIP_LOCATOR_LOCATION_SIZE];
} ShipLocatorRecord;

typedef enum ShipLocatorTargetStatus {
    SHIP_LOCATOR_TARGET_NONE = 0,
    SHIP_LOCATOR_TARGET_LOCAL,
    SHIP_LOCATOR_TARGET_REMOTE
} ShipLocatorTargetStatus;

typedef struct ShipLocatorTarget {
    ShipLocatorTargetStatus status;
    WorldDimension dimension;
    uint32_t surfaceId;
    Vector3 position;
    float distance;
    int blockX;
    int blockY;
    int blockZ;
    char location[SHIP_LOCATOR_LOCATION_SIZE];
} ShipLocatorTarget;

typedef struct ShipLocatorMarkerLayout {
    bool visible;
    bool onScreen;
    Vector2 position;
    Vector2 direction;
} ShipLocatorMarkerLayout;

void ShipLocatorReset(void);
bool ShipLocatorHasTarget(void);
ShipLocatorRecord ShipLocatorGetRecord(void);
bool ShipLocatorSetRecord(const ShipLocatorRecord *record);
bool ShipLocatorRecordParked(ShipLocatorContext context, int x, int y, int z,
                             const char *location);
bool ShipLocatorRemoveIfMatches(ShipLocatorContext context, int x, int y, int z);
bool ShipLocatorResolve(ShipLocatorContext context, Vector3 observer,
                        ShipLocatorTarget *out);

bool ShipLocatorSaveState(FILE *file);
bool ShipLocatorReadState(FILE *file, ShipLocatorRecord *out);
bool ShipLocatorReadStateForSpaceLayer(FILE *file, ShipLocatorRecord *out,
                                       int storedSpaceLayerY);
bool ShipLocatorLoadState(FILE *file);

ShipLocatorMarkerLayout ShipLocatorMarkerLayoutEvaluate(
    Vector2 projected, bool behind, int screenWidth, int screenHeight,
    float margin);

#endif
