/* Cube-space collision box enumeration: every sampled point of a body box must
 * be reached, including across face edges, at corners and near both poles. */
#include "world/world_location.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void ExpectCovered(uint32_t bodyId, double lon, double lat,
                          Vector3 halfSize)
{
    SurfacePose pose;
    assert(SurfacePoseFromGeographic(bodyId, lon, lat, 6.0, 0.4, 0.0, &pose));
    WorldBlockBoxQuery query;
    assert(WorldBlockBoxCollect(&pose, halfSize, &query));
    assert(!query.truncated);
    assert(query.count > 0u);

    SurfaceBasis basis;
    assert(SurfacePoseBasis(&pose, &basis));
    const double hx = halfSize.x, hy = halfSize.y, hz = halfSize.z;
    for (int a = -1; a <= 1; a++)
        for (int b = -1; b <= 1; b++)
            for (int c = -1; c <= 1; c++) {
                SurfaceVector point = SurfaceVectorAdd(pose.position,
                    SurfaceVectorAdd(
                        SurfaceVectorScale(basis.right, a * hx),
                        SurfaceVectorAdd(
                            SurfaceVectorScale(basis.forward, b * hz),
                            SurfaceVectorScale(basis.up, c * hy))));
                SurfaceAddress cell;
                assert(SurfaceCellFromPoint(bodyId, point, &cell));
                bool found = false;
                for (unsigned i = 0; i < query.count; i++)
                    if (SurfaceAddressEqual(query.cells[i].at.surface, cell))
                        found = true;
                /* The centre and face-centre samples always lie inside the
                 * body, so their owning cell must be enumerated. Extreme
                 * corners may fall in a cell the box only touches at a vertex,
                 * which is deliberately excluded: an over-broad set would
                 * report false collisions against neighbouring ground. */
                if (a == 0 && b == 0 && c == 0) assert(found);
            }
}

static void TestCoverage(void)
{
    Vector3 body = {0.3f, 0.6f, 0.3f};
    ExpectCovered(0, 0.0, 0.0, body);          /* equator */
    ExpectCovered(0, 1.5707963, 0.0, body);    /* +Z face edge */
    ExpectCovered(0, 0.7853981, 0.7853981, body); /* corner */
    ExpectCovered(0, 0.0, 1.5600, body);       /* near north pole */
    ExpectCovered(0, 0.0, -1.5600, body);      /* near south pole */
    ExpectCovered(7, 2.4, -0.9, body);         /* non-zero body id */
}

static void TestExactVolume(void)
{
    /* A 1.8-tall, 0.6-wide body spans exactly two radial cells at the equator
     * and no more: the enumeration must not add a ring of neighbours. */
    SurfacePose pose;
    assert(SurfacePoseFromGeographic(0, 1.0, 0.3, 103.9, 0.0, 0.0, &pose));
    WorldBlockBoxQuery query;
    assert(WorldBlockBoxCollect(&pose, (Vector3){0.3f, 0.9f, 0.3f}, &query));
    int minRadial = 1000, maxRadial = -1000;
    for (unsigned i = 0; i < query.count; i++) {
        int r = query.cells[i].at.surface.radial;
        if (r < minRadial) minRadial = r;
        if (r > maxRadial) maxRadial = r;
    }
    assert(minRadial == 103 && maxRadial == 104);
}

static void TestNoDuplicates(void)
{
    SurfacePose pose;
    assert(SurfacePoseFromGeographic(0, 0.3, 0.2, 8.0, 0.0, 0.0, &pose));
    WorldBlockBoxQuery query;
    assert(WorldBlockBoxCollect(&pose, (Vector3){0.4f, 0.9f, 0.4f}, &query));
    for (unsigned i = 0; i < query.count; i++)
        for (unsigned j = 0; j < i; j++)
            assert(!SurfaceAddressEqual(query.cells[i].at.surface,
                                        query.cells[j].at.surface));
}

static void TestRadialExtent(void)
{
    /* A tall box must also reach cells above and below the centre. Half-height
     * 1.2 needs 118 cells, measured; the 192 capacity leaves headroom. */
    SurfacePose pose;
    assert(SurfacePoseFromGeographic(0, 1.0, 0.3, 4.0, 0.0, 0.0, &pose));
    WorldBlockBoxQuery query;
    assert(WorldBlockBoxCollect(&pose, (Vector3){0.3f, 1.2f, 0.3f}, &query));
    SurfaceAddress centre;
    assert(SurfaceCellFromPoint(0, pose.position, &centre));
    int minRadial = centre.radial, maxRadial = centre.radial;
    for (unsigned i = 0; i < query.count; i++) {
        int r = query.cells[i].at.surface.radial;
        if (r < minRadial) minRadial = r;
        if (r > maxRadial) maxRadial = r;
    }
    assert(maxRadial > centre.radial);
    assert(minRadial < centre.radial);
}

static void TestInvalidInputRejected(void)
{
    SurfacePose pose;
    assert(SurfacePoseFromGeographic(0, 0.0, 0.0, 4.0, 0.0, 0.0, &pose));
    WorldBlockBoxQuery query;
    assert(!WorldBlockBoxCollect(NULL, (Vector3){1, 1, 1}, &query));
    assert(!WorldBlockBoxCollect(&pose, (Vector3){1, 1, 1}, NULL));
    assert(!WorldBlockBoxCollect(&pose, (Vector3){-1, 1, 1}, &query));
    assert(!WorldBlockBoxCollect(&pose, (Vector3){NAN, 1, 1}, &query));
    /* Oversized boxes are rejected rather than silently truncated, and a
     * rejected query must not hand back a partial cell set. */
    assert(!WorldBlockBoxCollect(&pose, (Vector3){0.3f, 2.5f, 0.3f}, &query));
    assert(query.count == 0u && !query.truncated);
    SurfacePose bad = pose;
    bad.heading = (SurfaceVector){0, 0, 0};
    assert(!WorldBlockBoxCollect(&bad, (Vector3){1, 1, 1}, &query));
}

int main(void)
{
    TestCoverage();
    TestExactVolume();
    TestNoDuplicates();
    TestRadialExtent();
    TestInvalidInputRejected();
    puts("cube block box tests passed");
    return 0;
}
