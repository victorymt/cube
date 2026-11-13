#include "gameplay/camera_distance.h"

#include "core/config.h"
#include "gameplay/camera.h"
#include "world/chunks.h"

#include <math.h>

static int CameraDistanceForHeight(float height)
{
    int extraDistance = (int)floorf(CameraHeightFactor(height) *
                                    (float)CAMERA_MAX_EXTRA_DISTANCE_CHUNKS);
    int effectiveDistance = ChunksRenderDistance() + extraDistance;
    if (effectiveDistance > MAX_RENDER_DISTANCE_CHUNKS)
        effectiveDistance = MAX_RENDER_DISTANCE_CHUNKS;
    return effectiveDistance;
}

int CameraRenderDistanceForHeight(float cameraHeight)
{
    return CameraDistanceForHeight(cameraHeight);
}

int CameraStreamingDistanceForHeight(float actorEyeHeight)
{
    return CameraDistanceForHeight(actorEyeHeight);
}
