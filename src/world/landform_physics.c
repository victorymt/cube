#include "world/landform_physics.h"
#include <math.h>
#include <string.h>

#define TAU 6.283185307179586476925286766559
static double Finite(double x) { return isfinite(x) ? x : 0; }
static bool Positive(double x) { return isfinite(x) && x > 0; }

bool LandformPhysicsValid(LandformPhysics p)
{
    return Positive(p.radiusMeters) && Positive(p.metersPerBlock) &&
        Positive(p.gravity) && isfinite(p.angularVelocity) &&
        Positive(p.radiusMeters / p.metersPerBlock);
}

double LandformAngularVelocity(double periodSeconds)
{
    return isfinite(periodSeconds) && periodSeconds != 0 ? Finite(TAU / periodSeconds) : 0;
}

double LandformDegreesPerSecondToRadians(double degrees)
{
    return Finite(degrees * (TAU / 360));
}

double LandformMetersToBlocks(double meters, double scale)
{
    return Positive(scale) ? Finite(meters / scale) : 0;
}

double LandformBlocksToMeters(double blocks, double scale)
{
    return Positive(scale) ? Finite(blocks * scale) : 0;
}

double LandformDischarge(double area, double runoff)
{
    return Positive(area) && Positive(runoff) ? Finite(area * runoff) : 0;
}

double LandformVelocity(double discharge, double width, double depth)
{
    return Positive(discharge) && Positive(width) && Positive(depth)
        ? Finite(discharge / width / depth) : 0;
}

LandformPhysicsDiagnostic LandformPhysicsDiagnose(LandformPhysics p,
    double latitude, double discharge, double width, double depth, double reach)
{
    LandformPhysicsDiagnostic d = {0};
    d.valid = LandformPhysicsValid(p) && isfinite(latitude) && fabs(latitude) <= TAU / 4;
    d.liquidWater = p.liquidWater;
    d.latitude = Finite(latitude);
    d.angularVelocity = Finite(p.angularVelocity);
    if (!d.valid) return d;
    d.coriolisParameter = Finite(2 * p.angularVelocity * sin(latitude));
    if (!p.liquidWater) return d;
    d.discharge = Positive(discharge) ? discharge : 0;
    d.velocity = LandformVelocity(d.discharge, width, depth);
    if (d.velocity < 1e-6 || !Positive(reach)) return d;
    // 1/2 * (f*v) * (L/v)^2; no division by tiny/zero flow.
    d.deflectionMeters = Finite(.5 * d.coriolisParameter * reach * (reach / d.velocity));
    d.deflectionBlocks = LandformMetersToBlocks(d.deflectionMeters, p.metersPerBlock);
    d.resolvableDeflection = fabs(d.deflectionBlocks) >= 1;
    return d;
}

uint64_t LandformFingerprintValue(uint64_t hash, double value)
{
    uint64_t bits = 0;
    if (isfinite(value) && value != 0) memcpy(&bits, &value, sizeof(bits));
    for (unsigned i = 0; i < 8; i++) {
        hash = (hash ^ (bits & 255)) * UINT64_C(1099511628211);
        bits >>= 8;
    }
    return hash;
}

uint64_t LandformPhysicsFingerprint(LandformPhysics p)
{
    uint64_t h = UINT64_C(14695981039346656037);
    h = LandformFingerprintValue(h, p.radiusMeters);
    h = LandformFingerprintValue(h, p.metersPerBlock);
    h = LandformFingerprintValue(h, p.gravity);
    h = LandformFingerprintValue(h, p.angularVelocity);
    return LandformFingerprintValue(h, p.liquidWater);
}
