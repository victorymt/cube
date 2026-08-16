#include "world/surface_topology.h"

#include "raymath.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static bool Near(float actual, float expected, float tolerance)
{
    return fabsf(actual - expected) <= tolerance;
}

static void TestFaceCenters(void)
{
    const Vector3 expected[SURFACE_FACE_COUNT] = {
        { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }
    };
    for (int face = 0; face < SURFACE_FACE_COUNT; face++) {
        SurfaceAddress address = {
            .bodyId = 7u,
            .face = (SurfaceFace)face,
            .u = SURFACE_FACE_BLOCKS / 2,
            .v = SURFACE_FACE_BLOCKS / 2
        };
        Vector3 direction = SurfaceAddressDirection(address);
        assert(Vector3DotProduct(direction, expected[face]) > 0.999f);
        SurfaceAddress restored = SurfaceAddressFromDirection(7u, direction, 0);
        assert(restored.face == address.face);
        assert(abs(restored.u - address.u) <= 1);
        assert(abs(restored.v - address.v) <= 1);
    }
}

static void TestEveryEdgeCrossesAndReturns(void)
{
    for (int face = 0; face < SURFACE_FACE_COUNT; face++) {
        for (int offset = 32; offset < SURFACE_FACE_BLOCKS; offset += 257) {
            SurfaceAddress edges[] = {
                { 1u, (SurfaceFace)face, 0, offset, 0 },
                { 1u, (SurfaceFace)face, SURFACE_FACE_BLOCKS - 1, offset, 0 },
                { 1u, (SurfaceFace)face, offset, 0, 0 },
                { 1u, (SurfaceFace)face, offset, SURFACE_FACE_BLOCKS - 1, 0 }
            };
            const int du[] = { -1, 1, 0, 0 };
            const int dv[] = { 0, 0, -1, 1 };
            for (int edge = 0; edge < 4; edge++) {
                SurfaceAddress next = SurfaceAddressOffset(edges[edge], du[edge],
                                                           dv[edge], 0);
                assert(SurfaceAddressIsValid(next));
                assert(next.face != edges[edge].face);
                Vector3 before = SurfaceAddressDirection(edges[edge]);
                Vector3 after = SurfaceAddressDirection(next);
                assert(Vector3DotProduct(before, after) > 0.999f);
            }
        }
    }
}

static void TestLatLonRoundTrip(void)
{
    for (int latitudeStep = -8; latitudeStep <= 8; latitudeStep++) {
        float latitude = (float)latitudeStep * PI / 18.0f;
        for (int longitudeStep = -18; longitudeStep <= 18; longitudeStep++) {
            float longitude = (float)longitudeStep * PI / 18.0f;
            SurfaceAddress address = SurfaceAddressFromLatLon(
                99u, longitude, latitude, 4);
            float actualLongitude;
            float actualLatitude;
            SurfaceAddressLatLon(address, &actualLongitude, &actualLatitude);
            float longitudeError = atan2f(sinf(actualLongitude - longitude),
                                          cosf(actualLongitude - longitude));
            assert(fabsf(longitudeError) < 0.004f);
            assert(fabsf(actualLatitude - latitude) < 0.0012f);
            assert(address.radial == 4);
        }
    }
}

static void TestMapCoordinateConversion(void)
{
    SurfaceAddress origin = SurfaceAddressFromMapCoordinates(7u, 0.0f, 0.0f, 3);
    SurfaceAddress wrapped = SurfaceAddressFromMapCoordinates(
        7u, (float)SURFACE_EQUATOR_BLOCKS, 0.0f, 3);
    assert(SurfaceAddressEqual(origin, wrapped));

    SurfaceAddress otherBody = SurfaceAddressFromMapCoordinates(
        8u, 0.0f, 0.0f, 3);
    assert(!SurfaceAddressEqual(origin, otherBody));
    assert(origin.face == otherBody.face);
    assert(origin.u == otherBody.u && origin.v == otherBody.v);

    SurfaceAddress northA = SurfaceAddressFromMapCoordinates(
        7u, 0.0f, 0.5f * (float)SURFACE_POLE_TO_POLE_BLOCKS, 0);
    SurfaceAddress northB = SurfaceAddressFromMapCoordinates(
        7u, 1234.0f, (float)SURFACE_POLE_TO_POLE_BLOCKS, 0);
    SurfaceAddress south = SurfaceAddressFromMapCoordinates(
        7u, 0.0f, -(float)SURFACE_POLE_TO_POLE_BLOCKS, 0);
    assert(northA.face == SURFACE_FACE_POS_Y);
    assert(northB.face == SURFACE_FACE_POS_Y);
    assert(south.face == SURFACE_FACE_NEG_Y);
}

static void TestFrameRoundTrip(void)
{
    SurfaceAddress address = SurfaceAddressFromLatLon(3u, 1.1f, -0.63f, 12);
    SurfaceFrame frame = SurfaceFrameAt(address);
    assert(Near(Vector3Length(frame.east), 1.0f, 0.0001f));
    assert(Near(Vector3Length(frame.north), 1.0f, 0.0001f));
    assert(Near(Vector3Length(frame.up), 1.0f, 0.0001f));
    assert(fabsf(Vector3DotProduct(frame.east, frame.north)) < 0.0001f);
    assert(fabsf(Vector3DotProduct(frame.east, frame.up)) < 0.0001f);
    assert(fabsf(Vector3DotProduct(frame.north, frame.up)) < 0.0001f);

    const Vector3 local = { 13.25f, -2.5f, 8.75f };
    Vector3 restored = SurfaceFramePlanetToLocal(
        &frame, SurfaceFrameLocalToPlanet(&frame, local));
    assert(Vector3Distance(local, restored) < 0.001f);

    SurfaceAddress nextAddress = SurfaceAddressOffset(address, 16, 0, 0);
    SurfaceFrame next = SurfaceFrameAt(nextAddress);
    Vector3 pointInNext = SurfaceFrameTransformPoint(&frame, &next, local);
    Vector3 pointBack = SurfaceFrameTransformPoint(&next, &frame, pointInNext);
    assert(Vector3Distance(local, pointBack) < 0.002f);
    Vector3 velocity = { 2.0f, 0.5f, -3.0f };
    Vector3 transformed = SurfaceFrameTransformVector(&frame, &next, velocity);
    assert(Near(Vector3Length(transformed), Vector3Length(velocity), 0.0005f));
}

static void TestPatchTransform(void)
{
    SurfaceAddress reference = SurfaceAddressFromMapCoordinates(
        4u, 0.0f, 0.0f, 0);
    SurfaceAddress patch = SurfaceAddressOffset(reference, 32, 0, 0);
    Matrix transform = SurfacePatchTransform(
        reference, (Vector3){ 10.0f, 0.0f, -5.0f }, patch, 20);
    Vector3 origin = Vector3Transform(Vector3Zero(), transform);
    assert(origin.x > 50.0f && origin.x < 53.0f);
    assert(origin.y > 19.0f && origin.y < 21.0f);
    assert(fabsf(origin.z + 5.0f) < 0.1f);

    Vector3 east = Vector3Subtract(
        Vector3Transform((Vector3){ 1.0f, 0.0f, 0.0f }, transform), origin);
    Vector3 up = Vector3Subtract(
        Vector3Transform((Vector3){ 0.0f, 1.0f, 0.0f }, transform), origin);
    assert(Near(Vector3Length(east), 1.0f, 0.0005f));
    assert(Near(Vector3Length(up), 1.0f, 0.0005f));
    assert(fabsf(Vector3DotProduct(east, up)) < 0.0005f);
}

static void TestContinuousPatchBoundary(void)
{
    const uint32_t bodyId = 11u;
    const int radialBase = 64;
    Vector2 referenceMap = { -40.0f, 25.0f };
    Vector2 patchA = { 0.0f, 0.0f };
    Vector2 patchB = { 16.0f, 0.0f };
    SurfaceFrame frameA = SurfaceFrameAtMapCoordinates(
        bodyId, patchA.x, patchA.y, radialBase);
    SurfaceFrame frameB = SurfaceFrameAtMapCoordinates(
        bodyId, patchB.x, patchB.y, radialBase);
    SurfaceFrame boundaryFrame = SurfaceFrameAtMapCoordinates(
        bodyId, 16.0f, 8.0f, radialBase);
    Vector3 planet = Vector3Add(
        boundaryFrame.origin, Vector3Scale(boundaryFrame.up, 5.0f));
    Vector3 localA = SurfaceFramePlanetToLocal(&frameA, planet);
    Vector3 localB = SurfaceFramePlanetToLocal(&frameB, planet);
    Matrix transformA = SurfacePatchTransformAtMap(
        bodyId, referenceMap, Vector3Zero(), patchA, radialBase);
    Matrix transformB = SurfacePatchTransformAtMap(
        bodyId, referenceMap, Vector3Zero(), patchB, radialBase);
    Vector3 renderedA = Vector3Transform(localA, transformA);
    Vector3 renderedB = Vector3Transform(localB, transformB);
    assert(Vector3Distance(renderedA, renderedB) < 0.001f);
}

int main(void)
{
    TestFaceCenters();
    TestEveryEdgeCrossesAndReturns();
    TestLatLonRoundTrip();
    TestMapCoordinateConversion();
    TestFrameRoundTrip();
    TestPatchTransform();
    TestContinuousPatchBoundary();
    puts("surface topology tests passed");
    return 0;
}
