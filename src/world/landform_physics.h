#ifndef VOXELCRAFT_LANDFORM_PHYSICS_H
#define VOXELCRAFT_LANDFORM_PHYSICS_H

#include <stdbool.h>
#include <stdint.h>

// SI units, independent of render/orbit scale. Rotation is signed radians/s.
typedef struct LandformPhysics {
    double radiusMeters, metersPerBlock, gravity, angularVelocity;
    bool liquidWater;
} LandformPhysics;

typedef struct LandformPhysicsDiagnostic {
    bool valid, liquidWater, resolvableDeflection;
    double latitude, angularVelocity, coriolisParameter;
    double discharge, velocity, deflectionMeters, deflectionBlocks;
} LandformPhysicsDiagnostic;

bool LandformPhysicsValid(LandformPhysics p);
double LandformAngularVelocity(double periodSeconds);
double LandformDegreesPerSecondToRadians(double degrees);
double LandformMetersToBlocks(double meters, double metersPerBlock);
double LandformBlocksToMeters(double blocks, double metersPerBlock);
double LandformDischarge(double areaSquareMeters, double runoffMetersPerSecond);
double LandformVelocity(double discharge, double widthMeters, double depthMeters);
// Diagnostic free-flight estimate over a reach, NOT a channel evolution model.
// Invalid parameters and effectively stagnant flows return finite zeros.
LandformPhysicsDiagnostic LandformPhysicsDiagnose(LandformPhysics p,
    double latitudeRadians, double discharge, double widthMeters,
    double depthMeters, double reachMeters);
// Stable field-wise hash: ignores padding, canonicalizes -0 and nonfinite values.
uint64_t LandformFingerprintValue(uint64_t hash, double value);
uint64_t LandformPhysicsFingerprint(LandformPhysics p);

#endif
