#include "world/surface_pose.h"
#include "world/world_location.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void TestProjection(void)
{
    const int coordinates[] = {0, 1, 2047, 2048, 4094, 4095};
    for (int face = 0; face < 6; face++)
        for (unsigned i = 0; i < 6; i++) for (unsigned j = 0; j < 6; j++) {
            SurfaceAddress cell = {123, (SurfaceFace)face, coordinates[i], coordinates[j], -20};
            SurfaceVector center;
            SurfaceAddress back;
            assert(SurfaceCellCenter(cell, &center));
            assert(SurfaceCellFromPoint(cell.bodyId, center, &back));
            assert(SurfaceAddressEqual(cell, back));
            SurfaceCoordinates c;
            assert(SurfaceCoordinatesFromPoint(center, &c));
            assert(fabs(c.u - cell.u - .5) < 1e-9);
            assert(fabs(c.v - cell.v - .5) < 1e-9);
            assert(fabs(c.radial + 19.5) < 1e-9);
        }
    // Exact corners and boundaries have one deterministic owner, with no
    // invalid u/v=N even when two or three dominant axes tie.
    for (int x = -1; x <= 1; x += 2) for (int y = -1; y <= 1; y += 2)
        for (int z = -1; z <= 1; z += 2) {
            SurfaceAddress cell;
            assert(SurfaceCellFromPoint(123, (SurfaceVector){3000*x, 3000*y, 3000*z}, &cell));
            assert(cell.face == (x > 0 ? SURFACE_FACE_POS_X : SURFACE_FACE_NEG_X));
            assert(SurfaceAddressIsValid(cell));
        }
}

static void TestTransport(void)
{
    for (int face = 0; face < 6; face++) for (int side = 0; side < 4; side++)
        for (int along = 0; along < SURFACE_FACE_BLOCKS; along++) {
            SurfaceAddress cell = {123, (SurfaceFace)face,
                side == 0 ? 0 : side == 1 ? 4095 : along,
                side == 2 ? 0 : side == 3 ? 4095 : along, 12};
            SurfaceAddress next;
            SurfaceCubeEdge back;
            assert(SurfaceCubeCellNeighbor(cell, (SurfaceCubeEdge)side, &next, &back));
            unsigned used = 0;
            for (int d = 0; d < 4; d++) {
                SurfaceCubeEdge moved, returned;
                assert(SurfaceCubeTransportEdge(cell, (SurfaceCubeEdge)side, (SurfaceCubeEdge)d, &moved));
                assert(SurfaceCubeTransportEdge(next, back, moved, &returned));
                assert(returned == (SurfaceCubeEdge)d);
                used |= 1u << moved;
            }
            assert(used == 15);
        }
}

static void TestMotion(void)
{
    SurfacePose pose;
    assert(SurfacePoseFromGeographic(7, .7, 0, 40, 0, .3, &pose));
    SurfacePose initial = pose;
    pose.velocity = SurfaceVectorScale(pose.heading, 4);
    double radius = SurfaceVectorLength(pose.position);
    // One full meridian crosses both poles and four face boundaries. Speed,
    // heading, pitch, height and the original point must be recovered.
    for (int i = 0; i < 20000; i++) {
        assert(SurfacePoseDisplace(&pose, 0, 2*acos(-1.0)*radius/20000, 0, &pose));
        assert(fabs(SurfaceVectorLength(pose.velocity) - 4) < 1e-9);
        assert(fabs(SurfaceVectorDot(pose.velocity, pose.heading) - 4) < 1e-9);
        assert(fabs(SurfacePoseAltitude(&pose) - 40) < 1e-7);
    }
    assert(SurfaceVectorLength(SurfaceVectorSubtract(pose.position, initial.position)) < 1e-6);
    assert(SurfaceVectorDot(pose.heading, initial.heading) > 1 - 1e-10);
    assert(pose.pitch == initial.pitch && pose.bodyId == initial.bodyId);
    SurfaceBasis basis;
    assert(SurfacePoseBasis(&pose, &basis));
    Vector3 offset = {1, 2, 3};
    Vector3 back = SurfacePointToLocal(&basis, SurfacePointFromLocal(&basis, offset));
    assert(fabsf(back.x - 1) < 1e-6 && fabsf(back.y - 2) < 1e-6 && fabsf(back.z - 3) < 1e-6);
    // Positive right at longitude 0 is geographic east, also at a pole.
    assert(SurfacePoseFromGeographic(7, 0, 0, 40, 0, 0, &pose));
    assert(SurfacePoseDisplace(&pose, 1, 0, 0, &pose));
    assert(pose.position.z > .99);
}

static void TestAddresses(void)
{
    WorldBlockAddress a = {.dimension = WORLD_DIMENSION_PLANET,
        .at.surface = {99, SURFACE_FACE_POS_Y, 4095, 0, -4}};
    WorldBlockAddress b, back;
    for (int side = 0; side < 6; side++) {
        WorldBlockSide returnSide;
        assert(WorldBlockNeighbor(a, (WorldBlockSide)side, &b, &returnSide));
        assert(WorldBlockNeighbor(b, returnSide, &back, NULL));
        assert(WorldBlockAddressEqual(a, back));
        assert(WorldBlockAddressHash(a) == WorldBlockAddressHash(back));
        assert(!WorldBlockAddressEqual(a, b));
    }
    b = a; b.at.surface.bodyId++;
    assert(!WorldBlockAddressEqual(a, b));
    assert(WorldBlockAddressHash(a) != WorldBlockAddressHash(b));
    b = a; b.dimension = WORLD_DIMENSION_HOME;
    assert(!WorldBlockAddressEqual(a, b));
    assert(WorldBlockAddressHash(a) != WorldBlockAddressHash(b));
    memset(&a, 0xa5, sizeof(a));
    a.dimension = WORLD_DIMENSION_SPACE;
    a.at.cartesian.x = 3; a.at.cartesian.y = -2; a.at.cartesian.z = 1;
    memset(&b, 0, sizeof(b));
    b.dimension = WORLD_DIMENSION_SPACE;
    b.at.cartesian.x = 3; b.at.cartesian.y = -2; b.at.cartesian.z = 1;
    assert(WorldBlockAddressEqual(a, b) && WorldBlockAddressHash(a) == WorldBlockAddressHash(b));
    a.at.cartesian.x = INT_MAX;
    assert(!WorldBlockNeighbor(a, WORLD_BLOCK_U_PLUS, &b, NULL));
}

static void TestInvalid(void)
{
    SurfacePose pose;
    assert(SurfacePoseFromGeographic(3, 0, 0, 1, 0, 0, &pose));
    SurfacePose before = pose;
    assert(!SurfacePoseDisplace(&pose, NAN, 0, 0, &pose));
    assert(!SurfacePoseDisplace(&pose, 0, 0, -1e9, &pose));
    assert(memcmp(&before, &pose, sizeof(pose)) == 0);
    assert(!SurfacePoseFromGeographic(0, 0, 5, 0, 0, 0, &pose));
    assert(!SurfacePoseFromGeographic(0, INFINITY, 0, 0, 0, 0, &pose));
    SurfaceAddress cell = {0}, beforeCell = cell;
    assert(!SurfaceCellFromPoint(0, (SurfaceVector){0}, &cell));
    assert(!SurfaceCellFromPoint(0, (SurfaceVector){NAN, 0, 0}, &cell));
    assert(memcmp(&cell, &beforeCell, sizeof(cell)) == 0);
}

int main(void)
{
    TestProjection(); TestTransport(); TestMotion(); TestAddresses(); TestInvalid();
    puts("surface pose and block address tests passed");
    return 0;
}
