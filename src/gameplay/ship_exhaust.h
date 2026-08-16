#ifndef VOXELCRAFT_SHIP_EXHAUST_H
#define VOXELCRAFT_SHIP_EXHAUST_H

#include "gameplay/ship.h"

typedef struct ShipExhaustProfile {
    float intensity;
    float flameLength;
    float outerRadius;
    float particleRate;
    Color coreColor;
    Color outerColor;
} ShipExhaustProfile;

ShipExhaustProfile ShipExhaustProfileFor(ShipDriveMode mode, float demand,
                                         float atmosphereDensity);
int ShipExhaustEmissionCount(float rate, float dt, float *carry,
                             int maximumPerFrame);
float ShipDustIntensity(float groundDistance, bool haveGround);

#endif
