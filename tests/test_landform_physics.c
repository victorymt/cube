#include "world/landform_physics.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    const double pi = acos(-1);
    assert(fabs(LandformAngularVelocity(86400) - 2*pi/86400) < 1e-15);
    assert(fabs(LandformDegreesPerSecondToRadians(180) - pi) < 1e-15);
    assert(LandformAngularVelocity(0) == 0);
    assert(LandformAngularVelocity(INFINITY) == 0);
    assert(LandformAngularVelocity(-86400) == -LandformAngularVelocity(86400));
    assert(LandformBlocksToMeters(4, 2.5) == 10);
    assert(LandformMetersToBlocks(10, 2.5) == 4);
    assert(LandformMetersToBlocks(-10, 2.5) == -4);
    assert(LandformMetersToBlocks(1, 0) == 0);
    assert(LandformBlocksToMeters(1, -1) == 0);
    assert(LandformBlocksToMeters(DBL_MAX, 2) == 0);
    assert(LandformDischarge(1000, .001) == 1);
    assert(LandformVelocity(12, 3, 2) == 2);
    assert(LandformVelocity(12, 0, 2) == 0);
    assert(LandformDischarge(NAN, 1) == 0);
    LandformPhysics p = {6371000, 2, 9.80665, 2*pi/86400, true};
    assert(LandformPhysicsValid(p));
    uint64_t hash = LandformPhysicsFingerprint(p);
    assert(hash == LandformPhysicsFingerprint(p));
    LandformPhysics q = p;
    q.angularVelocity *= 2;
    assert(hash != LandformPhysicsFingerprint(q));
    q = p; q.radiusMeters += 1;
    assert(hash != LandformPhysicsFingerprint(q));
    q = p; q.metersPerBlock += 1;
    assert(hash != LandformPhysicsFingerprint(q));
    q = p; q.gravity += 1;
    assert(hash != LandformPhysicsFingerprint(q));
    q = p; q.liquidWater = false;
    assert(hash != LandformPhysicsFingerprint(q));
    assert(LandformFingerprintValue(7, -0.0) == LandformFingerprintValue(7, 0.0));
    assert(LandformFingerprintValue(7, NAN) == LandformFingerprintValue(7, INFINITY));
    LandformPhysicsDiagnostic north = LandformPhysicsDiagnose(p, pi/4, 12, 3, 2, 1000);
    LandformPhysicsDiagnostic south = LandformPhysicsDiagnose(p, -pi/4, 12, 3, 2, 1000);
    assert(north.valid && north.resolvableDeflection);
    assert(north.velocity == 2 && north.discharge == 12);
    assert(north.deflectionMeters == -south.deflectionMeters);
    assert(north.deflectionBlocks == north.deflectionMeters / 2);
    assert(LandformPhysicsDiagnose(p, 0, 12, 3, 2, 1000).deflectionMeters == 0);
    assert(LandformPhysicsDiagnose(p, pi/4, 1e-20, 3, 2, 1000).deflectionMeters == 0);
    assert(LandformPhysicsDiagnose(p, pi/4, 0, 3, 2, 1000).velocity == 0);
    p.angularVelocity = 0;
    assert(LandformPhysicsDiagnose(p, pi/4, 12, 3, 2, 1000).deflectionMeters == 0);
    p.liquidWater = false;
    assert(LandformPhysicsDiagnose(p, pi/4, 12, 3, 2, 1000).discharge == 0);
    const double invalid[] = {0, -1, NAN, INFINITY};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); i++) {
        q = p; q.gravity = invalid[i];
        assert(!LandformPhysicsValid(q));
        LandformPhysicsDiagnostic d = LandformPhysicsDiagnose(q, pi/4, 1, 1, 1, 1);
        assert(!d.valid && isfinite(d.deflectionBlocks) && d.deflectionBlocks == 0);
        q = p; q.radiusMeters = invalid[i]; assert(!LandformPhysicsValid(q));
        q = p; q.metersPerBlock = invalid[i]; assert(!LandformPhysicsValid(q));
    }
    assert(!LandformPhysicsDiagnose(p, NAN, 1, 1, 1, 1).valid);
    puts("landform physics tests passed");
}
