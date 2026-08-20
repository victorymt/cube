#include "presentation/weather_optics.h"

#include "raymath.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void AssertNear(float actual, float expected, float tolerance)
{
    assert(isfinite(actual));
    assert(fabsf(actual - expected) <= tolerance);
}

static Vector3 RotateY(Vector3 value, float angle)
{
    float sine = sinf(angle);
    float cosine = cosf(angle);
    return (Vector3){
        value.x * cosine + value.z * sine,
        value.y,
        -value.x * sine + value.z * cosine
    };
}

static void AssertVectorNear(Vector3 actual, Vector3 expected, float tolerance)
{
    AssertNear(actual.x, expected.x, tolerance);
    AssertNear(actual.y, expected.y, tolerance);
    AssertNear(actual.z, expected.z, tolerance);
}

static void TestDirectionsStayOnAntiSolarCone(void)
{
    Vector3 sun = Vector3Normalize((Vector3){ 0.83f, 0.32f, 0.21f });
    Vector3 antiSolar = Vector3Negate(sun);
    float radius = 42.0f * DEG2RAD;
    for (int sample = 0; sample < 64; sample++) {
        float phase = (float)sample * 2.0f * PI / 64.0f;
        Vector3 direction = WeatherRainbowDirection(sun, radius, phase);
        AssertNear(Vector3Length(direction), 1.0f, 0.00001f);
        AssertNear(Vector3DotProduct(direction, antiSolar), cosf(radius),
                   0.00001f);
    }
}

static void TestPhaseZeroIsTopOfArc(void)
{
    Vector3 sun = Vector3Normalize((Vector3){ 0.94f, 0.28f, 0.18f });
    float radius = 42.0f * DEG2RAD;
    Vector3 top = WeatherRainbowDirection(sun, radius, 0.0f);
    assert(top.y > 0.0f);
    for (int sample = 1; sample < 128; sample++) {
        float phase = (float)sample * 2.0f * PI / 128.0f;
        Vector3 direction = WeatherRainbowDirection(sun, radius, phase);
        assert(direction.y <= top.y + 0.00001f);
    }
}

static void TestArcRotatesWithSun(void)
{
    Vector3 firstSun = Vector3Normalize((Vector3){ 0.91f, 0.34f, 0.12f });
    float rotation = 0.5f * PI;
    Vector3 secondSun = RotateY(firstSun, rotation);
    for (int sample = 0; sample < 16; sample++) {
        float phase = (float)sample * 2.0f * PI / 16.0f;
        Vector3 first = WeatherRainbowDirection(
            firstSun, 41.0f * DEG2RAD, phase);
        Vector3 second = WeatherRainbowDirection(
            secondSun, 41.0f * DEG2RAD, phase);
        AssertVectorNear(second, RotateY(first, rotation), 0.00001f);
    }
}

static void TestInvalidInputsAreRejected(void)
{
    AssertVectorNear(WeatherRainbowDirection(Vector3Zero(), 0.7f, 0.0f),
                     Vector3Zero(), 0.0f);
    AssertVectorNear(WeatherRainbowDirection(
                         (Vector3){ NAN, 0.0f, 1.0f }, 0.7f, 0.0f),
                     Vector3Zero(), 0.0f);
    AssertVectorNear(WeatherRainbowDirection(
                         (Vector3){ 1.0f, 0.0f, 0.0f }, NAN, 0.0f),
                     Vector3Zero(), 0.0f);
}

int main(void)
{
    TestDirectionsStayOnAntiSolarCone();
    TestPhaseZeroIsTopOfArc();
    TestArcRotatesWithSun();
    TestInvalidInputsAreRejected();
    puts("weather optics tests passed");
    return 0;
}
