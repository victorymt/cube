#include "gameplay/ship_exhaust.h"

#include "raymath.h"

#include <math.h>

static unsigned char MixChannel(unsigned char from, unsigned char to,
                                float amount)
{
    return (unsigned char)Clamp((float)from + ((float)to - (float)from) *
                                amount, 0.0f, 255.0f);
}

static Color MixColor(Color from, Color to, float amount)
{
    amount = Clamp(amount, 0.0f, 1.0f);
    return (Color){
        MixChannel(from.r, to.r, amount),
        MixChannel(from.g, to.g, amount),
        MixChannel(from.b, to.b, amount),
        MixChannel(from.a, to.a, amount)
    };
}

ShipExhaustProfile ShipExhaustProfileFor(ShipDriveMode mode, float demand,
                                         float atmosphereDensity)
{
    ShipExhaustProfile profile = {
        .intensity = 0.0f,
        .flameLength = 0.72f,
        .outerRadius = 0.15f,
        .particleRate = 10.0f,
        .coreColor = { 226, 247, 255, 240 },
        .outerColor = { 76, 156, 255, 170 }
    };
    if (!isfinite(demand) || !isfinite(atmosphereDensity) ||
        mode < SHIP_DRIVE_MANEUVER ||
        mode > SHIP_DRIVE_INTERSTELLAR_WARP) {
        return profile;
    }
    profile.intensity = Clamp(demand, 0.0f, 1.0f);
    switch (mode) {
    case SHIP_DRIVE_ORBIT:
        profile.intensity = 0.0f;
        break;
    case SHIP_DRIVE_MANUAL_CRUISE:
        profile.flameLength = 0.90f;
        profile.outerRadius = 0.16f;
        profile.particleRate = 12.0f;
        break;
    case SHIP_DRIVE_APPROACH:
        profile.flameLength = 0.82f;
        profile.outerRadius = 0.16f;
        profile.particleRate = 12.0f;
        break;
    case SHIP_DRIVE_SUPERCRUISE:
        profile.flameLength = 1.10f;
        profile.outerRadius = 0.18f;
        profile.particleRate = 15.0f;
        break;
    case SHIP_DRIVE_INTERSTELLAR_WARP:
        profile.flameLength = 1.35f;
        profile.outerRadius = 0.19f;
        profile.particleRate = 18.0f;
        profile.coreColor = (Color){ 232, 255, 255, 245 };
        profile.outerColor = (Color){ 86, 210, 255, 180 };
        return profile;
    case SHIP_DRIVE_MANEUVER:
    default:
        break;
    }
    Color atmosphericEdge = { 255, 166, 84, 145 };
    profile.outerColor = MixColor(
        profile.outerColor, atmosphericEdge,
        Clamp(atmosphereDensity, 0.0f, 1.0f) * 0.16f);
    return profile;
}

int ShipExhaustEmissionCount(float rate, float dt, float *carry,
                             int maximumPerFrame)
{
    if (!carry || !isfinite(*carry) || *carry < 0.0f ||
        !isfinite(rate) || rate <= 0.0f || !isfinite(dt) || dt <= 0.0f ||
        maximumPerFrame <= 0) {
        if (carry && (!isfinite(*carry) || *carry < 0.0f)) *carry = 0.0f;
        return 0;
    }
    *carry += rate * dt;
    int count = (int)floorf(*carry);
    if (count <= 0) return 0;
    *carry -= (float)count;
    if (count > maximumPerFrame) {
        count = maximumPerFrame;
        *carry = 0.0f;
    }
    return count;
}

float ShipDustIntensity(float groundDistance, bool haveGround)
{
    if (!haveGround || !isfinite(groundDistance) ||
        groundDistance >= 6.0f) {
        return 0.0f;
    }
    if (groundDistance <= 1.25f) return 1.0f;
    float amount = Clamp((6.0f - groundDistance) / 4.75f, 0.0f, 1.0f);
    return amount * amount * (3.0f - 2.0f * amount);
}
