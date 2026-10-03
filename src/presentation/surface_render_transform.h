#ifndef VOXELCRAFT_SURFACE_RENDER_TRANSFORM_H
#define VOXELCRAFT_SURFACE_RENDER_TRANSFORM_H
#include "world/world_types.h"

/* Shared by terrain, shadows and overlays positioned in section-local space. */
Matrix SurfaceChunkRenderTransform(const Chunk *chunk, int sectionY,
                                    Vector3 cameraPosition,
                                    int mapOriginX, int mapOriginZ);
#endif
