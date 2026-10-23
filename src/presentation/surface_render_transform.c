#include "presentation/surface_render_transform.h"
#include "raymath.h"
#include "world/surface_coverage.h"

Matrix SurfaceChunkRenderTransform(const Chunk *chunk, int sectionY,
                                    Vector3 cameraPosition,
                                    int mapOriginX, int mapOriginZ)
{
    int radialBase = sectionY * SURFACE_SECTION_HEIGHT;
    if (!chunk->spherical) {
        return MatrixTranslate((float)(chunk->cx * CHUNK_SIZE), (float)radialBase,
                               (float)(chunk->cz * CHUNK_SIZE));
    }
    if (chunk->cubeGrid) {
        SurfaceVector anchor;
        if (!SurfacePointFromCoordinates((SurfaceCoordinates){chunk->cubeKey.face,
            chunk->cubeKey.u * CHUNK_SIZE, chunk->cubeKey.v * CHUNK_SIZE,
            radialBase}, &anchor)) return MatrixIdentity();
        /* The frame must sit at the camera's own radius. Building it at radial 0
         * put its origin at the planet centre, so every section was offset by a
         * whole sphere radius and the terrain rendered as floating plates. */
        SurfaceFrame frame = SurfaceFrameAtMapCoordinates(chunk->cubeKey.bodyId,
            mapOriginX + cameraPosition.x, mapOriginZ + cameraPosition.z,
            (int)lroundf(cameraPosition.y));
        Vector3 relative = {(float)(anchor.x - frame.origin.x),
            (float)(anchor.y - frame.origin.y), (float)(anchor.z - frame.origin.z)};
        return (Matrix){
            .m0 = frame.east.x, .m4 = frame.east.y, .m8 = frame.east.z,
            .m1 = frame.up.x, .m5 = frame.up.y, .m9 = frame.up.z,
            .m2 = frame.north.x, .m6 = frame.north.y, .m10 = frame.north.z,
            .m12 = cameraPosition.x + Vector3DotProduct(relative, frame.east),
            .m13 = Vector3DotProduct(relative, frame.up),
            .m14 = cameraPosition.z + Vector3DotProduct(relative, frame.north), .m15 = 1};
    }
    return SurfacePatchTransformAtMap(chunk->surfaceAddress.bodyId,
        (Vector2){ mapOriginX + cameraPosition.x, mapOriginZ + cameraPosition.z },
        (Vector3){ cameraPosition.x, 0.0f, cameraPosition.z },
        (Vector2){ (float)(mapOriginX + chunk->cx * CHUNK_SIZE),
                   (float)(mapOriginZ + chunk->cz * CHUNK_SIZE) }, radialBase);
}

bool SurfaceChunkRenderBounds(const Chunk *chunk, int sectionY,
                               Vector3 *outCenter, float *outRadius)
{
    if (!chunk || !outCenter || !outRadius) return false;
    if (chunk->cubeGrid) {
        SurfaceVector anchor, center;
        double radius, radial = sectionY * SURFACE_SECTION_HEIGHT;
        if (!SurfacePointFromCoordinates((SurfaceCoordinates){chunk->cubeKey.face,
            chunk->cubeKey.u * CHUNK_SIZE, chunk->cubeKey.v * CHUNK_SIZE, radial}, &anchor) ||
            !SurfaceCubeChunkBounds(chunk->cubeKey, radial, radial + SURFACE_SECTION_HEIGHT,
                                     &center, &radius)) return false;
        center = SurfaceVectorSubtract(center, anchor);
        *outCenter = (Vector3){(float)center.x, (float)center.y, (float)center.z};
        *outRadius = (float)radius + .001f;
    } else {
        float half = SURFACE_SECTION_HEIGHT * .5f;
        *outCenter = (Vector3){CHUNK_SIZE * .5f, half, CHUNK_SIZE * .5f};
        *outRadius = sqrtf(half * half * 3);
    }
    return true;
}
