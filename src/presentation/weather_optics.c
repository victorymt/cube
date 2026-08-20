#include "presentation/weather_optics.h"

#include "raymath.h"

#include <math.h>

Vector3 WeatherRainbowDirection(Vector3 sunDirection,
                                float angularRadiusRadians,
                                float phaseRadians)
{
    if (!isfinite(sunDirection.x) || !isfinite(sunDirection.y) ||
        !isfinite(sunDirection.z) || !isfinite(angularRadiusRadians) ||
        !isfinite(phaseRadians) ||
        Vector3LengthSqr(sunDirection) < 0.000001f) {
        return Vector3Zero();
    }

    Vector3 antiSolar = Vector3Negate(Vector3Normalize(sunDirection));
    Vector3 basisUp = Vector3Subtract(
        (Vector3){ 0.0f, 1.0f, 0.0f },
        Vector3Scale(antiSolar, antiSolar.y));
    if (Vector3LengthSqr(basisUp) < 0.000001f) {
        basisUp = (Vector3){ 1.0f, 0.0f, 0.0f };
    } else {
        basisUp = Vector3Normalize(basisUp);
    }
    Vector3 basisRight = Vector3Normalize(
        Vector3CrossProduct(antiSolar, basisUp));
    Vector3 radial = Vector3Add(
        Vector3Scale(basisUp, cosf(phaseRadians)),
        Vector3Scale(basisRight, sinf(phaseRadians)));
    return Vector3Normalize(Vector3Add(
        Vector3Scale(antiSolar, cosf(angularRadiusRadians)),
        Vector3Scale(radial, sinf(angularRadiusRadians))));
}
