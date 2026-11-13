/* Landing clearance in cube space: the ground cell must be known and solid,
 * and every occupied body cell must be known and clear. Unknown terrain is
 * never treated as air. */
#include "world/world_location.h"
#include "surface_contract_test_stub.h"

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
    /* Measured near the pole: a 1.8-tall body occupies only cell 103. Meridians
     * converge there, so a cell is a tiny fraction of a block and the flood
     * cannot reach the neighbours a mid-latitude body spans. The ground rule
     * must therefore be geometric rather than a fixed cell count, and it must
     * hold for the cells that are actually reported. */
    FakeWorld world = {.groundRadial = 102, .knownBelow = 0, .solid = true};
    SurfacePose pose;
    float bodyCenterRadial = 103.0f + half.y;
    assert(SurfacePoseFromGeographic(0, 0.0, 1.5690, bodyCenterRadial,
                                     0.0, 0.0, &pose));
    assert(WorldBlockLocationClear(&pose, half, BoxSolid, BoxKnown, &world));
    /* And a solid cell inside the reported span must block it. */
    world.groundRadial = 103;
    assert(!WorldBlockLocationClear(&pose, half, BoxSolid, BoxKnown, &world));
}

/* Legacy map cells must resolve to distinct cube cells. This is what lets
 * map-indexed consumers (fluid, ecology, terrain queries) read a cube world
 * without changing their own coordinate convention. */
static void TestMapCellsMapToDistinctCubeCells(void)
{
    SurfaceAddress seen[400];
    unsigned count = 0;
    for (int x = 0; x < 20; x++) {
        for (int z = 0; z < 20; z++) {
            Vector3 direction = SurfaceDirectionFromMapCoordinates(
                (float)(x * 97), (float)(z * 97));
            SurfaceAddress cell = SurfaceAddressFromDirection(0, direction, 64);
            assert(SurfaceAddressIsValid(cell));
            /* No two distinct map cells may land on the same cube cell. */
            for (unsigned i = 0; i < count; i++)
                assert(!(seen[i].face == cell.face && seen[i].u == cell.u &&
                         seen[i].v == cell.v));
            seen[count++] = cell;
        }
    }
    assert(count == 400u);
}

int main(void)
{
    assert(InstallTestSurfaceContract(0));
    TestStandingOnGroundIsClear();
    TestFloatingIsNotClear();
    TestUnknownTerrainRejected();
    TestBodyInsideGroundRejected();
    TestNoSolidGroundRejected();
    TestInvalidArgumentsRejected();
    TestPolarLandingIsClear();
    TestMapCellsMapToDistinctCubeCells();
    assert(WorldInstallSurfaceSnapshot(NULL));
    puts("cube landing clearance tests passed");
    return 0;
}
