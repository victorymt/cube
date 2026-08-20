#include "presentation/surface_globe.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static float AngleDistance(float a, float b)
{
    return fabsf(atan2f(sinf(a - b), cosf(a - b)));
}

static void TestCenterHit(void)
{
    Rectangle destination = { 100.0f, 40.0f, 240.0f, 240.0f };
    float longitude = 0.0f;
    float latitude = 0.0f;
    float cameraLongitude = -0.82f;
    float cameraLatitude = 0.31f;
    assert(SurfaceGlobeHitTest(
        destination, cameraLongitude, cameraLatitude,
        (Vector2){ 220.0f, 160.0f }, &longitude, &latitude));
    assert(AngleDistance(longitude, cameraLongitude) < 0.001f);
    assert(fabsf(latitude - cameraLatitude) < 0.001f);
}

static void TestEdgeAndOutsideHits(void)
{
    Rectangle destination = { 10.0f, 20.0f, 200.0f, 200.0f };
    float longitude = 0.0f;
    float latitude = 0.0f;
    assert(SurfaceGlobeHitTest(
        destination, 0.0f, 0.0f, (Vector2){ 110.0f, 25.0f },
        &longitude, &latitude));
    assert(latitude > 0.9f);
    assert(!SurfaceGlobeHitTest(
        destination, 0.0f, 0.0f, (Vector2){ 12.0f, 22.0f },
        &longitude, &latitude));
    assert(!SurfaceGlobeHitTest(
        destination, 0.0f, 0.0f, (Vector2){ 400.0f, 400.0f },
        &longitude, &latitude));
}

int main(void)
{
    TestCenterHit();
    TestEdgeAndOutsideHits();
    puts("surface globe tests passed");
    return 0;
}
