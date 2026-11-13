#ifndef VOXELCRAFT_WORLD_FRAME_H
#define VOXELCRAFT_WORLD_FRAME_H

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum WorldFrameKind {
    WORLD_FRAME_LOCAL = 0,
    WORLD_FRAME_BODY_CENTERED,
    WORLD_FRAME_SYSTEM,
    WORLD_FRAME_RENDER_CAMERA
} WorldFrameKind;

typedef struct WorldFrameId {
    WorldFrameKind kind;
    uint32_t bodyId;
    int64_t systemAnchorX;
    int64_t systemAnchorZ;
} WorldFrameId;

typedef struct WorldFramePoint {
    WorldFrameId frame;
    double x;
    double y;
    double z;
} WorldFramePoint;

WorldFrameId WorldFrameSystemId(int64_t systemAnchorX, int64_t systemAnchorZ);
WorldFrameId WorldFrameSaveId(uint32_t bodyId);
bool WorldFrameIdEqual(WorldFrameId a, WorldFrameId b);
bool WorldFrameCanTransform(WorldFrameId source, WorldFrameId target);
bool WorldFrameTransformPoint(const WorldFramePoint *source,
                              WorldFrameId target, double simulationTime,
                              WorldFramePoint *out);
bool WorldFrameTransformPointToCamera(const WorldFramePoint *source,
                                      Vector3 cameraLocalPosition,
                                      double simulationTime,
                                      WorldFramePoint *out);
bool WorldFrameTransformPointFromCamera(const WorldFramePoint *source,
                                        Vector3 cameraLocalPosition,
                                        double simulationTime,
                                        WorldFramePoint *out);

#endif
