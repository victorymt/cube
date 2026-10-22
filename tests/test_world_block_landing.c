/* Landing clearance in cube space: the ground cell must be known and solid,
 * and every occupied body cell must be known and clear. Unknown terrain is
 * never treated as air. */
#include "world/world_location.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

/* A compound literal is not an lvalue, so the box half-size needs a name. */
static const Vector3 half = {0.3f, 0.9f, 0.3f};

typedef struct FakeWorld {
    int groundRadial;   /* cells at or below this radial are solid */
    int knownBelow;     /* cells below this radial are ungenerated */
    bool solid;
} FakeWorld;

static bool CellSolid(WorldBlockAddress cell, void *user)
{
    const FakeWorld *world = user;
    return world->solid && cell.at.surface.radial <= world->groundRadial;
}

static bool BoxKnown(const WorldBlockBoxQuery *box, void *user)
{
    const FakeWorld *world = user;
    for (unsigned i = 0; i < box->count; i++)
        if (box->cells[i].at.surface.radial < world->knownBelow) return false;
    return true;
}

static bool BoxSolid(const WorldBlockBoxQuery *box, void *user)
{
    for (unsigned i = 0; i < box->count; i++)
        if (CellSolid(box->cells[i], user)) return true;
    return false;
}

/* `radial` is the BODY CENTRE height; the box spans +/- half.y around it. */
static void MakePose(double lon, double lat, int radial, SurfacePose *out)
{
    assert(SurfacePoseFromGeographic(0, lon, lat, (double)radial, 0.0, 0.0,
                                     out));
}

static void TestStandingOnGroundIsClear(void)
{
    /* Ground occupies radial 102 (i.e. 102.0..103.0), so the feet sit at 103.0
     * and the body centre is 103.9 for a 1.8-tall body. The body then spans
     * 103.0..104.8 and never overlaps the ground cell. */
    FakeWorld world = {.groundRadial = 102, .knownBelow = 0, .solid = true};
    SurfacePose pose;
    assert(SurfacePoseFromGeographic(0, 1.0, 0.3, 103.9, 0.0, 0.0, &pose));
    assert(WorldBlockLocationClear(&pose, half, BoxSolid, BoxKnown, &world));
}

static void TestFloatingIsNotClear(void)
{
    FakeWorld world = {.groundRadial = 100, .knownBelow = 0, .solid = true};
    SurfacePose pose;
    MakePose(1.0, 0.3, 141, &pose);
    assert(!WorldBlockLocationClear(&pose, half, BoxSolid, BoxKnown, &world));
}

static void TestUnknownTerrainRejected(void)
{
    FakeWorld world = {.groundRadial = -1000, .knownBelow = 500, .solid = true};
    SurfacePose pose;
    MakePose(1.0, 0.3, 103, &pose);
    assert(!WorldBlockLocationClear(&pose, half, BoxSolid, BoxKnown, &world));
}

static void TestBodyInsideGroundRejected(void)
{
    FakeWorld world = {.groundRadial = 103, .knownBelow = 0, .solid = true};
    SurfacePose pose;
    MakePose(1.0, 0.3, 103, &pose);
    assert(!WorldBlockLocationClear(&pose, half, BoxSolid, BoxKnown, &world));
}

static void TestNoSolidGroundRejected(void)
{
    FakeWorld world = {.groundRadial = 100, .knownBelow = 0, .solid = false};
    SurfacePose pose;
    MakePose(1.0, 0.3, 103, &pose);
    /* Without a solid ground cell this is not a landing spot. */
    assert(!WorldBlockLocationClear(&pose, half, BoxSolid, BoxKnown, &world));
}

static void TestInvalidArgumentsRejected(void)
{
    FakeWorld world = {.groundRadial = 101, .knownBelow = 0, .solid = true};
    SurfacePose pose;
    MakePose(1.0, 0.3, 103, &pose);
    assert(!WorldBlockLocationClear(NULL, half, BoxSolid, BoxKnown, &world));
    assert(!WorldBlockLocationClear(&pose, half, NULL, BoxKnown, &world));
    assert(!WorldBlockLocationClear(&pose, half, BoxSolid, NULL, &world));
}

static void TestPolarLandingIsClear(void)
{
    /* Measured near the pole: a 1.8-tall body spans only cell 103, so the
     * ground cell is 102. Cells are tiny here, which is exactly why the rule
     * must be geometric rather than a fixed cell count. */
    FakeWorld world = {.groundRadial = 102, .knownBelow = 0, .solid = true};
    SurfacePose pose;
    assert(SurfacePoseFromGeographic(0, 0.0, 1.5690, 103.5, 0.0, 0.0, &pose));
    /* Near a pole the cells are tiny, but the ground rule must still hold. */
    assert(WorldBlockLocationClear(&pose, half, BoxSolid, BoxKnown, &world));
}

int main(void)
{
    TestStandingOnGroundIsClear();
    TestFloatingIsNotClear();
    TestUnknownTerrainRejected();
    TestBodyInsideGroundRejected();
    TestNoSolidGroundRejected();
    TestInvalidArgumentsRejected();
    TestPolarLandingIsClear();
    puts("cube landing clearance tests passed");
    return 0;
}
