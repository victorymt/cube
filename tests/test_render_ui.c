#include "presentation/render_ui.h"

#include "raylib.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void AssertNear(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.001f);
}

static void TestHeadingFromYaw(void)
{
    AssertNear(HudHeadingFromYaw(180.0f * DEG2RAD), 0.0f);
    AssertNear(HudHeadingFromYaw(135.0f * DEG2RAD), 45.0f);
    AssertNear(HudHeadingFromYaw(90.0f * DEG2RAD), 90.0f);
    AssertNear(HudHeadingFromYaw(45.0f * DEG2RAD), 135.0f);
    AssertNear(HudHeadingFromYaw(0.0f), 180.0f);
    AssertNear(HudHeadingFromYaw(-45.0f * DEG2RAD), 225.0f);
    AssertNear(HudHeadingFromYaw(-90.0f * DEG2RAD), 270.0f);
    AssertNear(HudHeadingFromYaw(-135.0f * DEG2RAD), 315.0f);
    AssertNear(HudHeadingFromYaw(-180.0f * DEG2RAD), 0.0f);
}

static void TestHeadingDirections(void)
{
    assert(strcmp(HudHeadingDirection(0.0f), "N") == 0);
    assert(strcmp(HudHeadingDirection(22.499f), "N") == 0);
    assert(strcmp(HudHeadingDirection(22.5f), "NE") == 0);
    assert(strcmp(HudHeadingDirection(67.5f), "E") == 0);
    assert(strcmp(HudHeadingDirection(112.5f), "SE") == 0);
    assert(strcmp(HudHeadingDirection(157.5f), "S") == 0);
    assert(strcmp(HudHeadingDirection(202.5f), "SW") == 0);
    assert(strcmp(HudHeadingDirection(247.5f), "W") == 0);
    assert(strcmp(HudHeadingDirection(292.5f), "NW") == 0);
    assert(strcmp(HudHeadingDirection(337.5f), "N") == 0);
    assert(strcmp(HudHeadingDirection(-45.0f), "NW") == 0);
}

static void TestStatusLine(void)
{
    char status[128];
    HudFormatStatusLine(status, sizeof(status),
                        (Vector3){ 1.9f, -0.1f, -2.0f },
                        180.0f * DEG2RAD, -14.0f * DEG2RAD, 0.25f);
    assert(strcmp(status,
                  "XYZ 1 -1 -2   N 000   P-14   06:00") == 0);

    HudFormatStatusLine(status, sizeof(status), (Vector3){ 0 },
                        -179.6f * DEG2RAD, 0.0f, 1.0f);
    assert(strcmp(status,
                  "XYZ 0 0 0   N 000   P+00   00:00") == 0);
}

int main(void)
{
    TestHeadingFromYaw();
    TestHeadingDirections();
    TestStatusLine();
    puts("render UI tests passed");
    return 0;
}
