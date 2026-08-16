#include "space/space_coordinates.h"
#include "space/space_units.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static bool Near(double actual, double expected, double tolerance)
{
    return fabs(actual - expected) <= tolerance;
}

int main(void)
{
    CelestialVector3 oneAu = {
        SPACE_UNITS_ASTRONOMICAL_UNIT_KM, 0.0, 0.0
    };
    Vector3 projected = CelestialVectorToGameDistance(oneAu);
    assert(Near(projected.x, SPACE_UNITS_GAME_DISTANCE_PER_AU, 1.0e-5));
    CelestialVector3 restored = CelestialVectorFromGameDistance(projected);
    assert(Near(restored.x, oneAu.x, 8.0));

    CelestialPosition camera = { .systemAnchorX = 12, .systemAnchorZ = -4,
        .offsetKm = { 1.0e12, -2.0e11, 4.0e12 } };
    CelestialPosition body = camera;
    body.offsetKm.x += SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
    body.offsetKm.z -= SPACE_UNITS_ASTRONOMICAL_UNIT_KM * 0.5;
    Vector3 local;
    assert(CelestialProjectRelative(body, camera, &local));
    assert(Near(local.x, SPACE_UNITS_GAME_DISTANCE_PER_AU, 1.0e-4));
    assert(Near(local.z, -SPACE_UNITS_GAME_DISTANCE_PER_AU * 0.5, 1.0e-4));

    body.systemAnchorX++;
    assert(!CelestialProjectRelative(body, camera, &local));
    puts("space coordinate tests passed");
    return 0;
}
