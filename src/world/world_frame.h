#ifndef VOXELCRAFT_WORLD_FRAME_H
#define VOXELCRAFT_WORLD_FRAME_H

#include <stdbool.h>
#include <stdint.h>

typedef enum WorldFrameKind {
    WORLD_FRAME_LOCAL = 0,
    WORLD_FRAME_BODY_CENTERED,
    WORLD_FRAME_SYSTEM
} WorldFrameKind;

typedef struct WorldFrameId {
    WorldFrameKind kind;
    uint32_t bodyId;
} WorldFrameId;

typedef struct WorldFramePoint {
    WorldFrameId frame;
    double x;
    double y;
    double z;
} WorldFramePoint;

bool WorldFrameIdEqual(WorldFrameId a, WorldFrameId b);
bool WorldFrameCanTransform(WorldFrameId source, WorldFrameId target);
bool WorldFrameTransformPoint(const WorldFramePoint *source,
                              WorldFrameId target, double simulationTime,
                              WorldFramePoint *out);

#endif
