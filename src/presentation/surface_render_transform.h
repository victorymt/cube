#ifndef VOXELCRAFT_SURFACE_RENDER_TRANSFORM_H
#define VOXELCRAFT_SURFACE_RENDER_TRANSFORM_H
#include "world/chunk_types.h"

/* Shared by terrain, shadows and overlays positioned in section-local space. */
Matrix SurfaceChunkRenderTransform(const Chunk *chunk, int sectionY,
                                    Vector3 cameraPosition,
                                    int mapOriginX, int mapOriginZ);
bool SurfaceChunkRenderBounds(const Chunk *chunk, int sectionY,
                               Vector3 *outCenter, float *outRadius);
// Exact cube-cell corners in the same camera-local reference frame as
// SurfaceChunkRenderTransform. Corner order matches SurfaceCellCorners().
bool SurfaceCubeCellRenderCorners(SurfaceAddress cell, Vector3 cameraPosition,
                                  int mapOriginX, int mapOriginZ,
                                  Vector3 outCorners[8]);
#endif
