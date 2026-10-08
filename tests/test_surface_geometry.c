#include "world/surface_geometry.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static SurfacePose Above(SurfaceAddress cell, double altitude)
{
    SurfacePose pose = {.bodyId = cell.bodyId};
    assert(SurfacePointFromCoordinates((SurfaceCoordinates){cell.face, cell.u + .5, cell.v + .5, altitude}, &pose.position));
    SurfaceVector up = SurfaceVectorNormalize(pose.position);
    SurfaceVector axis = fabs(up.y) > .9 ? (SurfaceVector){1, 0, 0} : (SurfaceVector){0, 1, 0};
    pose.heading = SurfaceVectorNormalize(SurfaceVectorSubtract(axis, SurfaceVectorScale(up, SurfaceVectorDot(axis, up))));
    return pose;
}

static void TestQueries(void)
{
    const int positions[] = {0, 2048, 4095};
    for (int face = 0; face < 6; face++) for (int u = 0; u < 3; u++) for (int v = 0; v < 3; v++) {
        SurfaceAddress cell = {9, (SurfaceFace)face, positions[u], positions[v], 12};
        SurfacePose pose = Above(cell, 22);
        SurfaceBasis basis;
        assert(SurfacePoseBasis(&pose, &basis));
        SurfaceVector down = SurfaceVectorScale(basis.up, -1);
        SurfaceGeometryHit hit;
        assert(SurfaceRayCellBox(cell, SURFACE_CELL_BOX_FULL, pose.position, down, 12, &hit));
        assert(hit.side == WORLD_BLOCK_UP);
        assert(hit.distance >= 9 && hit.distance < 9.001);
        assert(SurfaceVectorDot(hit.normal, basis.up) > .999);
        assert(!SurfaceRayCellBox(cell, SURFACE_CELL_BOX_FULL, pose.position, down, 8, &hit));
        assert(!SurfaceRayCellBox(cell, SURFACE_CELL_BOX_FULL, pose.position, basis.up, 20, &hit));

        SurfaceCellBox slab = {0, 0, 0, 1, 1, .5};
        assert(SurfaceRayCellBox(cell, slab, pose.position, down, 12, &hit));
        assert(hit.side == WORLD_BLOCK_UP && hit.distance >= 9.5 && hit.distance < 9.501);

        // A 20-block fall in one step must collide with the same projected
        // shape seen by the ray, even at a corner or on either polar face.
        assert(SurfaceSweepCellBox(cell, SURFACE_CELL_BOX_FULL, &basis,
            (Vector3){.2f, .9f, .2f}, (Vector3){0, -20, 0}, &hit));
        assert(hit.distance > .4049 && hit.distance < .4051);
        assert(SurfaceVectorDot(hit.normal, basis.up) > .999);
        assert(SurfaceSweepCellBox(cell, slab, &basis,
            (Vector3){.2f, .9f, .2f}, (Vector3){0, -20, 0}, &hit));
        assert(hit.distance > .4299 && hit.distance < .4301);
        assert(!SurfaceSweepCellBox(cell, slab, &basis,
            (Vector3){.2f, .9f, .2f}, (Vector3){0, 20, 0}, &hit));

        SurfaceTriangle triangles[12];
        assert(SurfaceCellTriangles(cell, SURFACE_CELL_BOX_FULL, triangles));
        SurfaceVector center;
        assert(SurfaceCellCenter(cell, &center));
        for (int i = 0; i < 12; i++) {
            SurfaceTriangle t = triangles[i];
            SurfaceVector n = SurfaceVectorCross(SurfaceVectorSubtract(t.b, t.a), SurfaceVectorSubtract(t.c, t.a));
            assert(SurfaceVectorDot(n, SurfaceVectorSubtract(t.a, center)) > 0);
        }
    }
}

static void TestInvalid(void)
{
    SurfaceAddress cell = {9, SURFACE_FACE_POS_Y, 2048, 2048, 12};
    SurfaceGeometryHit hit = {.distance = 999}, before = hit;
    SurfaceCellBox box = SURFACE_CELL_BOX_FULL;
    box.maxU = NAN;
    assert(!SurfaceRayCellBox(cell, box, (SurfaceVector){0, 1, 0}, (SurfaceVector){0, -1, 0}, 20, &hit));
    assert(memcmp(&hit, &before, sizeof(hit)) == 0);
    SurfacePose pose = Above(cell, 22);
    SurfaceBasis basis;
    assert(SurfacePoseBasis(&pose, &basis));
    basis.up = (SurfaceVector){0};
    assert(!SurfaceSweepCellBox(cell, SURFACE_CELL_BOX_FULL, &basis,
        (Vector3){1, 1, 1}, (Vector3){0, -20, 0}, &hit));
    assert(memcmp(&hit, &before, sizeof(hit)) == 0);
}

int main(void)
{
    TestQueries(); TestInvalid();
    puts("surface geometry ray and sweep tests passed");
    return 0;
}
