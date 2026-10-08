#include "world/surface_coverage.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void TestCoverage(void)
{
    SurfaceCoverageCandidate all[SURFACE_COVERAGE_MAX_CANDIDATES], bounded[625];
    const double positions[] = {0, 2048, 4096};
    unsigned maximum = 0;
    for (int face = 0; face < 6; face++) for (int u = 0; u < 3; u++) for (int v = 0; v < 3; v++) {
        SurfaceVector focus;
        assert(SurfacePointFromCoordinates((SurfaceCoordinates){(SurfaceFace)face, positions[u], positions[v], 100}, &focus));
        SurfaceCoverageResult full, small;
        assert(SurfaceCoverageCollect(9, focus, 208, all, SURFACE_COVERAGE_MAX_CANDIDATES, &full));
        assert(!full.saturated && full.admitted == full.requested && full.admitted > 200);
        assert(SurfaceCoverageCollect(9, focus, 208, bounded, 625, &small));
        assert(small.requested == full.requested && small.admitted <= 625);
        assert(small.saturated == (full.requested > 625));
        if (full.requested > maximum) maximum = full.requested;
        for (unsigned i = 0; i < full.admitted; i++) {
            assert(all[i].key.bodyId == 9 && SurfaceCubeChunkKeyIsValid(all[i].key));
            assert(all[i].minimumDistance <= 208.000001);
            if (i) assert(all[i].centerDistance >= all[i - 1].centerDistance);
            if (i < small.admitted) assert(SurfaceCubeChunkKeyEqual(all[i].key, bounded[i].key));
            for (unsigned j = 0; j < i; j++) assert(!SurfaceCubeChunkKeyEqual(all[i].key, all[j].key));
        }
        // Independently sample the requested cap in physical directions.
        SurfaceVector up = SurfaceVectorNormalize(focus);
        SurfaceVector axis = fabs(up.y) > .9 ? (SurfaceVector){1, 0, 0} : (SurfaceVector){0, 1, 0};
        SurfaceVector north = SurfaceVectorNormalize(SurfaceVectorSubtract(axis, SurfaceVectorScale(up, SurfaceVectorDot(up, axis))));
        SurfaceVector east = SurfaceVectorCross(up, north);
        for (int a = 0; a < 72; a++) for (int r = 0; r <= 4; r++) {
            double bearing = a*acos(-1.0)/36, angle = r*52.0/SURFACE_RADIUS_BLOCKS;
            SurfaceVector tangent = SurfaceVectorAdd(SurfaceVectorScale(north, cos(bearing)), SurfaceVectorScale(east, sin(bearing)));
            SurfaceVector point = SurfaceVectorScale(SurfaceVectorAdd(SurfaceVectorScale(up, cos(angle)),
                SurfaceVectorScale(tangent, sin(angle))), SURFACE_RADIUS_BLOCKS);
            SurfaceAddress cell; SurfaceCubeChunkKey key;
            assert(SurfaceCellFromPoint(9, point, &cell) && SurfaceCubeChunkAt(cell, &key));
            bool found = false;
            for (unsigned i = 0; i < full.admitted; i++) if (SurfaceCubeChunkKeyEqual(all[i].key, key)) found = true;
            assert(found);
        }
    }
    assert(maximum > 625);
    printf("cube coverage maximum requested=%u; budget saturation reported\n", maximum);
}

static void TestBounds(void)
{
    for (int face = 0; face < 6; face++) for (int u = 0; u < 256; u += 17) for (int v = 0; v < 256; v += 17) {
        SurfaceCubeChunkKey key = {9, (SurfaceFace)face, u, v};
        SurfaceVector center; double radius;
        assert(SurfaceCubeChunkBounds(key, -2048, 256, &center, &radius));
        for (int i = 0; i <= 4; i++) for (int j = 0; j <= 4; j++) for (int h = 0; h <= 2; h++) {
            SurfaceVector point;
            assert(SurfacePointFromCoordinates((SurfaceCoordinates){key.face, u*16 + i*4, v*16 + j*4, -2048 + h*1152}, &point));
            assert(SurfaceVectorLength(SurfaceVectorSubtract(point, center)) <= radius);
        }
    }
}

int main(void)
{
    TestCoverage(); TestBounds();
    puts("surface physical coverage tests passed");
    return 0;
}
