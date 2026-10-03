#include "presentation/surface_render_transform.h"
#include "raymath.h"

Matrix SurfaceChunkRenderTransform(const Chunk *chunk, int sectionY,
                                    Vector3 cameraPosition,
                                    int mapOriginX, int mapOriginZ)
{
    int radialBase = sectionY * SURFACE_SECTION_HEIGHT;
    if (!chunk->spherical) {
        return MatrixTranslate((float)(chunk->cx * CHUNK_SIZE), (float)radialBase,
                               (float)(chunk->cz * CHUNK_SIZE));
    }
    return SurfacePatchTransformAtMap(chunk->surfaceAddress.bodyId,
        (Vector2){ mapOriginX + cameraPosition.x, mapOriginZ + cameraPosition.z },
        (Vector3){ cameraPosition.x, 0.0f, cameraPosition.z },
        (Vector2){ (float)(mapOriginX + chunk->cx * CHUNK_SIZE),
                   (float)(mapOriginZ + chunk->cz * CHUNK_SIZE) }, radialBase);
}
