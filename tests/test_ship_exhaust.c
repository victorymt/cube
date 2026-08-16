#include "gameplay/ship_exhaust.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void TestDriveProfiles(void)
{
    ShipExhaustProfile idle = ShipExhaustProfileFor(
        SHIP_DRIVE_MANEUVER, 0.0f, 0.0f);
    assert(idle.intensity == 0.0f);
    assert(idle.flameLength <= 0.75f);

    ShipExhaustProfile maneuver = ShipExhaustProfileFor(
        SHIP_DRIVE_MANEUVER, 0.65f, 0.0f);
    ShipExhaustProfile cruise = ShipExhaustProfileFor(
        SHIP_DRIVE_MANUAL_CRUISE, 0.8f, 0.0f);
    ShipExhaustProfile supercruise = ShipExhaustProfileFor(
        SHIP_DRIVE_SUPERCRUISE, 1.0f, 0.0f);
    ShipExhaustProfile warp = ShipExhaustProfileFor(
        SHIP_DRIVE_INTERSTELLAR_WARP, 1.0f, 0.0f);
    assert(maneuver.intensity == 0.65f);
    assert(maneuver.flameLength < cruise.flameLength);
    assert(cruise.flameLength < supercruise.flameLength);
    assert(supercruise.flameLength < warp.flameLength);
    assert(warp.flameLength <= 1.35f);
    assert(warp.particleRate > cruise.particleRate);

    ShipExhaustProfile orbit = ShipExhaustProfileFor(
        SHIP_DRIVE_ORBIT, 1.0f, 0.0f);
    assert(orbit.intensity == 0.0f);
    ShipExhaustProfile atmosphere = ShipExhaustProfileFor(
        SHIP_DRIVE_MANEUVER, 1.0f, 1.0f);
    assert(atmosphere.outerColor.r > maneuver.outerColor.r);
    assert(atmosphere.outerColor.b < maneuver.outerColor.b);
}

static int EmitForDuration(float rate, float dt, int frames)
{
    float carry = 0.0f;
    int total = 0;
    for (int frame = 0; frame < frames; frame++) {
        total += ShipExhaustEmissionCount(rate, dt, &carry, 8);
    }
    return total;
}

static void TestFrameRateIndependentEmission(void)
{
    int at30 = EmitForDuration(18.0f, 1.0f / 30.0f, 120);
    int at60 = EmitForDuration(18.0f, 1.0f / 60.0f, 240);
    int at144 = EmitForDuration(18.0f, 1.0f / 144.0f, 576);
    assert(at30 == 72);
    assert(at60 == at30);
    assert(at144 == at30);

    float carry = 0.0f;
    assert(ShipExhaustEmissionCount(1000.0f, 1.0f, &carry, 8) == 8);
    assert(carry == 0.0f);
    assert(ShipExhaustEmissionCount(NAN, 1.0f, &carry, 8) == 0);
}

static void TestDustDistance(void)
{
    assert(ShipDustIntensity(0.5f, true) == 1.0f);
    assert(ShipDustIntensity(6.0f, true) == 0.0f);
    assert(ShipDustIntensity(2.0f, false) == 0.0f);
    float near = ShipDustIntensity(2.0f, true);
    float far = ShipDustIntensity(5.0f, true);
    assert(near > far && far > 0.0f);
    assert(ShipDustIntensity(NAN, true) == 0.0f);
}

int main(void)
{
    TestDriveProfiles();
    TestFrameRateIndependentEmission();
    TestDustDistance();
    puts("ship exhaust tests passed");
    return 0;
}
