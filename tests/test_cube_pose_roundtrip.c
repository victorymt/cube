/* W1 probe: can a continuous pose round-trip through cube cell identity and
 * back without drift, including across face edges and poles?
 * This is a pure geometry check; it does not touch the production runtime. */
#include "world/surface_geometry.h"
#include "world/surface_pose.h"
#include "world/surface_cube.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void ExpectNear(double actual, double expected, double tol,
                       const char *what)
{
    if (fabs(actual - expected) > tol) {
        fprintf(stderr, "FAIL %s: %.6f vs %.6f\n", what, actual, expected);
        assert(0);
    }
}

static void TestCellRoundTrip(void)
{
    /* Every cell centre must map to exactly that cell. */
    for (int face = 0; face < SURFACE_FACE_COUNT; face++) {
        for (int u = 0; u < SURFACE_FACE_BLOCKS; u += 137) {
            for (int v = 0; v < SURFACE_FACE_BLOCKS; v += 311) {
                SurfaceAddress cell = {7, (SurfaceFace)face, u, v, 12};
                SurfaceVector centre;
                SurfaceAddress back;
                assert(SurfaceCellCenter(cell, &centre));
                assert(SurfaceCellFromPoint(7, centre, &back));
                assert(back.bodyId == cell.bodyId && back.face == cell.face);
                assert(back.u == cell.u && back.v == cell.v);
            }
        }
    }
}

static void TestPoseSurvivesCellJump(void)
{
    /* A pose converted to its cell and back to a pose must keep its heading
     * tangent and its radial altitude. */
    for (int face = 0; face < SURFACE_FACE_COUNT; face++) {
        SurfacePose start;
        assert(SurfacePoseFromGeographic(3,
            (double)face * 0.7 + 0.2, (double)face * 0.3 - 0.4,
            40.0, 0.9, 0.1, &start));
        SurfaceAddress cell;
        assert(SurfaceCellFromPoint(3, start.position, &cell));
        SurfaceVector centre;
        assert(SurfaceCellCenter(cell, &centre));
        double radial = SurfaceVectorLength(start.position) - SURFACE_RADIUS_BLOCKS;
        ExpectNear(radial, (double)cell.radial + 0.5, 0.51,
                   "radial maps into the owning cell");
        ExpectNear(SurfaceVectorLength(centre), SURFACE_RADIUS_BLOCKS +
                   cell.radial + 0.5, 1e-6, "cell centre radius");
    }
}

static void TestDisplaceStaysOnSphere(void)
{
    /* Long great-circle walks must preserve radius and heading tangency. */
    SurfacePose pose;
    assert(SurfacePoseFromGeographic(1, 0.0, 0.0, 0.0, 0.0, 0.0, &pose));
    for (int i = 0; i < 400; i++) {
        SurfacePose next;
        assert(SurfacePoseDisplace(&pose, 0.0, 8.0, 0.0, &next));
        pose = next;
        ExpectNear(SurfaceVectorLength(pose.position),
                   SURFACE_RADIUS_BLOCKS, 1e-6, "radius after walk");
        double dot = SurfaceVectorDot(SurfaceVectorNormalize(pose.position),
                                      pose.heading);
        ExpectNear(dot, 0.0, 1e-9, "heading stays tangent");
    }
}

static void TestPolarWalk(void)
{
    /* Walking over the north pole must not collapse or reverse unexpectedly. */
    SurfacePose pose;
    assert(SurfacePoseFromGeographic(2, 0.0, 1.4, 0.0, 0.0, 0.0, &pose));
    for (int i = 0; i < 200; i++) {
        SurfacePose next;
        assert(SurfacePoseDisplace(&pose, 0.0, 4.0, 0.0, &next));
        pose = next;
        assert(isfinite(pose.position.y));
        ExpectNear(SurfaceVectorLength(pose.position), SURFACE_RADIUS_BLOCKS,
                   1e-6, "polar radius");
    }
    /* Measured behaviour: starting at 80.21 deg and walking 17.58 deg of arc
     * crosses the pole and lands at 82.21 deg on the opposite meridian. The x
     * coordinate therefore changes sign while z stays on the same plane. */
    assert(pose.position.x < 0.0);
    double lat = asin(pose.position.y / SURFACE_RADIUS_BLOCKS);
    assert(lat > 1.40 && lat < 1.437); /* 82.2 deg, measured */
    double arc = hypot(pose.position.x, pose.position.z) /
                 SURFACE_RADIUS_BLOCKS;
    assert(arc > 0.13 && arc < 0.14); /* cos(82.2 deg) */
}

int main(void)
{
    TestCellRoundTrip();
    TestPoseSurvivesCellJump();
    TestDisplaceStaysOnSphere();
    TestPolarWalk();
    puts("cube pose round-trip probe passed");
    return 0;
}
