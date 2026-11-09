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
        /* Mesh vertices are in the section anchor's tangent frame. Transform
         * that same frame into the camera-local reference frame used by every
         * other spherical draw; mixing camera axes with an anchor origin makes
         * vertical faces stretch into the visible green wall. */
        SurfaceFrame patch = SurfaceFrameAt((SurfaceAddress){
            chunk->cubeKey.bodyId, chunk->cubeKey.face,
            chunk->cubeKey.u * CHUNK_SIZE,
            chunk->cubeKey.v * CHUNK_SIZE, radialBase
        });
        patch.origin = (Vector3){
            (float)anchor.x, (float)anchor.y, (float)anchor.z
        };
        SurfaceFrame reference = SurfaceFrameAtMapCoordinates(
            chunk->cubeKey.bodyId, mapOriginX + cameraPosition.x,
            mapOriginZ + cameraPosition.z, 0);
        Vector3 east = SurfaceFrameTransformVector(
            &patch, &reference, (Vector3){ 1.0f, 0.0f, 0.0f });
        Vector3 up = SurfaceFrameTransformVector(
            &patch, &reference, (Vector3){ 0.0f, 1.0f, 0.0f });
        Vector3 north = SurfaceFrameTransformVector(
            &patch, &reference, (Vector3){ 0.0f, 0.0f, 1.0f });
        Vector3 origin = SurfaceFramePlanetToLocal(&reference, patch.origin);
        origin.x += cameraPosition.x;
        origin.z += cameraPosition.z;
        return (Matrix){
            .m0 = east.x, .m1 = east.y, .m2 = east.z,
            .m4 = up.x, .m5 = up.y, .m6 = up.z,
            .m8 = north.x, .m9 = north.y, .m10 = north.z,
            .m12 = origin.x, .m13 = origin.y, .m14 = origin.z,
            .m15 = 1.0f
        };
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
        /* Bounds are consumed in the same anchor tangent frame as mesh
         * vertices. A raw Cartesian center passes the wrong point to frustum
         * culling, most visibly near poles and face edges. */
        SurfaceFrame patch = SurfaceFrameAt((SurfaceAddress){
            chunk->cubeKey.bodyId, chunk->cubeKey.face,
            chunk->cubeKey.u * CHUNK_SIZE,
            chunk->cubeKey.v * CHUNK_SIZE, radial
        });
        SurfaceVector offset = SurfaceVectorSubtract(center, anchor);
        *outCenter = (Vector3){
            (float)SurfaceVectorDot(offset, (SurfaceVector){
                (double)patch.east.x, (double)patch.east.y,
                (double)patch.east.z }),
            (float)SurfaceVectorDot(offset, (SurfaceVector){
                (double)patch.up.x, (double)patch.up.y,
                (double)patch.up.z }),
            (float)SurfaceVectorDot(offset, (SurfaceVector){
                (double)patch.north.x, (double)patch.north.y,
                (double)patch.north.z })
        };
        *outRadius = (float)radius + .001f;
    } else {
        float half = SURFACE_SECTION_HEIGHT * .5f;
        *outCenter = (Vector3){CHUNK_SIZE * .5f, half, CHUNK_SIZE * .5f};
        *outRadius = sqrtf(half * half * 3);
    }
    return true;
}
