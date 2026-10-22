/* Cube selection ray: it must find the same cells collision uses, stop at the
 * first solid block, and respect the distance limit. The real cube edit store
 * supplies the solid cells, so this exercises production lookup too. */
#include "world/cube_raycast.h"
#include "world/world_cube.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

/* Focused fixture: no resident chunks, so marking a section dirty is a no-op.
 * The production definition lives in the chunk runtime, which this test does
 * not link. */
void MarkGeneratedSectionAndNeighborsDirty(void *chunk, int sectionY)
{
    (void)chunk;
    (void)sectionY;
}

static WorldBlockAddress Cell(SurfaceAddress surface)
{
    WorldBlockAddress address;
    address.dimension = surface.bodyId == 0 ? WORLD_DIMENSION_HOME
                                            : WORLD_DIMENSION_PLANET;
    address.at.surface = surface;
    return address;
}

/* Fills one cell along the radial axis under `probe`; returns its address. */
static WorldBlockAddress FillBelow(SurfacePose probe, int drop)
{
    SurfaceAddress under;
    assert(SurfaceCellFromPoint(0, probe.position, &under));
    under.radial -= drop;
    assert(WorldCubeSetBlock(Cell(under), BLOCK_STONE));
    return Cell(under);
}

static SurfacePose PoseAt(double lon, double lat, double radial)
{
    SurfacePose pose;
    assert(SurfacePoseFromGeographic(0, lon, lat, radial, 0.0, 0.0, &pose));
    return pose;
}

static SurfaceVector Down(SurfacePose pose)
{
    return SurfaceVectorScale(SurfaceVectorNormalize(pose.position), -1.0);
}

static void TestDownwardHitFindsGround(void)
{
    WorldCubeClearEdits();
    SurfacePose pose = PoseAt(0.5, 0.2, 10.0);
    WorldBlockAddress ground = FillBelow(pose, 3);

    CubeRaycastHit hit;
    assert(CubeRaycast(0, pose.position, Down(pose), 20.0, &hit));
    assert(hit.valid);
    assert(WorldBlockAddressEqual(hit.address, ground));
    assert(hit.geometry.distance > 0.0 && hit.geometry.distance <= 20.0);
    WorldCubeClearEdits();
}

static void TestDistanceLimitRespected(void)
{
    WorldCubeClearEdits();
    SurfacePose pose = PoseAt(0.5, 0.2, 10.0);
    FillBelow(pose, 6);
    CubeRaycastHit hit;
    /* The drop is about six blocks; a two-block ray must miss. */
    assert(!CubeRaycast(0, pose.position, Down(pose), 2.0, &hit));
    assert(!hit.valid);
    /* A long enough ray finds it. */
    assert(CubeRaycast(0, pose.position, Down(pose), 12.0, &hit));
    WorldCubeClearEdits();
}

static void TestNoSolidDoesNotHit(void)
{
    WorldCubeClearEdits();
    SurfacePose pose = PoseAt(0.5, 0.2, 10.0);
    CubeRaycastHit hit;
    /* Nothing was placed anywhere. */
    assert(!CubeRaycast(0, pose.position, Down(pose), 20.0, &hit));
    WorldCubeClearEdits();
}

static void TestInvalidInput(void)
{
    WorldCubeClearEdits();
    SurfacePose pose = PoseAt(0.5, 0.2, 10.0);
    FillBelow(pose, 3);
    CubeRaycastHit hit;
    assert(!CubeRaycast(0, pose.position, pose.position, 10.0, &hit));
    assert(!CubeRaycast(0, pose.position, (SurfaceVector){0, 0, 0}, 10.0, &hit));
    assert(!CubeRaycast(0, pose.position, Down(pose), 0.0, &hit));
    assert(!CubeRaycast(0, pose.position, Down(pose), NAN, &hit));
    assert(!CubeRaycast(0, (SurfaceVector){NAN, 0, 0}, Down(pose), 10.0, &hit));
    assert(!CubeRaycast(0, pose.position, Down(pose), 10.0, NULL));
    WorldCubeClearEdits();
}

static void TestHorizontalMiss(void)
{
    WorldCubeClearEdits();
    SurfacePose pose = PoseAt(0.5, 0.2, 30.0);
    FillBelow(pose, 5);
    SurfaceBasis basis;
    assert(SurfacePoseBasis(&pose, &basis));
    CubeRaycastHit hit;
    /* Level shot above the ground clears it. */
    assert(!CubeRaycast(0, pose.position, basis.forward, 8.0, &hit));
    WorldCubeClearEdits();
}

static void TestCrossFaceRay(void)
{
    WorldCubeClearEdits();
    /* Face 0 at u=4095 is adjacent to face 5 at u=0 (verified by probing the
     * neighbour contract). Placing the target on the far face proves the march
     * does not stop at a face boundary. */
    SurfaceAddress near = {0, SURFACE_FACE_POS_X, 4095, 2048, 20};
    SurfaceAddress far;
    SurfaceCubeEdge back;
    assert(SurfaceCubeCellNeighbor(near, SURFACE_CUBE_U_PLUS, &far, &back));
    assert(far.face != near.face);

    SurfaceVector from, to;
    assert(SurfaceCellCenter(near, &from));
    assert(SurfaceCellCenter(far, &to));
    assert(WorldCubeSetBlock(Cell(far), BLOCK_STONE));

    CubeRaycastHit hit;
    assert(CubeRaycast(0, from, SurfaceVectorSubtract(to, from), 8.0, &hit));
    assert(WorldBlockAddressEqual(hit.address, Cell(far)));
    assert(hit.address.at.surface.face == far.face);
    WorldCubeClearEdits();
}

int main(void)
{
    TestDownwardHitFindsGround();
    TestDistanceLimitRespected();
    TestNoSolidDoesNotHit();
    TestInvalidInput();
    TestHorizontalMiss();
    TestCrossFaceRay();
    puts("cube raycast tests passed");
    return 0;
}
