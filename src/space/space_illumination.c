#include "space/space_illumination.h"

#include "space/space_units.h"

#include <math.h>

#define SPACE_ILLUMINATION_PI 3.14159265358979323846

static double SpaceIlluminationClamp(double value, double minimum,
                                     double maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static double SpaceIlluminationPositiveFiniteOrZero(double value)
{
    return value > 0.0 && isfinite(value) ? value : 0.0;
}

static bool SpaceIlluminationVectorIsFinite(SpaceIlluminationVector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static double SpaceIlluminationDot(SpaceIlluminationVector3 left,
                                   SpaceIlluminationVector3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

static double SpaceIlluminationLength(SpaceIlluminationVector3 value)
{
    return sqrt(SpaceIlluminationDot(value, value));
}

double SpaceIlluminationIrradianceEarth(double luminositySolar,
                                        double distanceKm)
{
    if (!(luminositySolar > 0.0) || !(distanceKm > 0.0) ||
        !isfinite(luminositySolar) || !isfinite(distanceKm)) {
        return 0.0;
    }
    double distanceAu = distanceKm / SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
    return SpaceIlluminationPositiveFiniteOrZero(
        luminositySolar / (distanceAu * distanceAu));
}

double SpaceIlluminationOrbitMeanIrradianceEarth(
    double luminositySolar, double semiMajorAxisKm, double eccentricity)
{
    if (!(semiMajorAxisKm > 0.0) || eccentricity < 0.0 ||
        eccentricity >= 1.0 || !isfinite(eccentricity)) {
        return 0.0;
    }
    double circularIrradiance = SpaceIlluminationIrradianceEarth(
        luminositySolar, semiMajorAxisKm);
    return SpaceIlluminationPositiveFiniteOrZero(
        circularIrradiance /
        sqrt(1.0 - eccentricity * eccentricity));
}

double SpaceIlluminationCircleCoverage(double targetAngularRadius,
                                       double occulterAngularRadius,
                                       double angularSeparation)
{
    if (!(targetAngularRadius > 0.0) || !(occulterAngularRadius > 0.0) ||
        !isfinite(targetAngularRadius) ||
        !isfinite(occulterAngularRadius) ||
        !isfinite(angularSeparation)) {
        return 0.0;
    }
    angularSeparation = fabs(angularSeparation);
    if (angularSeparation >= targetAngularRadius + occulterAngularRadius) {
        return 0.0;
    }
    if (angularSeparation <=
        fabs(targetAngularRadius - occulterAngularRadius)) {
        if (occulterAngularRadius >= targetAngularRadius) return 1.0;
        return SpaceIlluminationPositiveFiniteOrZero(
            (occulterAngularRadius * occulterAngularRadius) /
            (targetAngularRadius * targetAngularRadius));
    }

    double targetTerm = SpaceIlluminationClamp(
        (angularSeparation * angularSeparation +
         targetAngularRadius * targetAngularRadius -
         occulterAngularRadius * occulterAngularRadius) /
            (2.0 * angularSeparation * targetAngularRadius),
        -1.0, 1.0);
    double occulterTerm = SpaceIlluminationClamp(
        (angularSeparation * angularSeparation +
         occulterAngularRadius * occulterAngularRadius -
         targetAngularRadius * targetAngularRadius) /
            (2.0 * angularSeparation * occulterAngularRadius),
        -1.0, 1.0);
    double radical =
        (-angularSeparation + targetAngularRadius + occulterAngularRadius) *
        (angularSeparation + targetAngularRadius - occulterAngularRadius) *
        (angularSeparation - targetAngularRadius + occulterAngularRadius) *
        (angularSeparation + targetAngularRadius + occulterAngularRadius);
    double overlapArea =
        targetAngularRadius * targetAngularRadius * acos(targetTerm) +
        occulterAngularRadius * occulterAngularRadius * acos(occulterTerm) -
        0.5 * sqrt(fmax(radical, 0.0));
    return SpaceIlluminationPositiveFiniteOrZero(SpaceIlluminationClamp(
        overlapArea /
            (SPACE_ILLUMINATION_PI * targetAngularRadius *
             targetAngularRadius),
        0.0, 1.0));
}

double SpaceIlluminationOccultationFraction(
    SpaceIlluminationBody foreground, SpaceIlluminationBody background)
{
    if (!SpaceIlluminationVectorIsFinite(foreground.positionKm) ||
        !SpaceIlluminationVectorIsFinite(background.positionKm) ||
        !isfinite(foreground.radiusKm) || !isfinite(background.radiusKm)) {
        return 0.0;
    }
    double foregroundDistance = SpaceIlluminationLength(
        foreground.positionKm);
    double backgroundDistance = SpaceIlluminationLength(
        background.positionKm);
    if (!(foreground.radiusKm > 0.0) || !(background.radiusKm > 0.0) ||
        !isfinite(foregroundDistance) || !isfinite(backgroundDistance) ||
        !(foregroundDistance > foreground.radiusKm) ||
        !(backgroundDistance > background.radiusKm) ||
        foregroundDistance >= backgroundDistance) {
        return 0.0;
    }

    double foregroundAngularRadius = asin(SpaceIlluminationClamp(
        foreground.radiusKm / foregroundDistance, 0.0, 1.0));
    double backgroundAngularRadius = asin(SpaceIlluminationClamp(
        background.radiusKm / backgroundDistance, 0.0, 1.0));
    double alignment = SpaceIlluminationDot(
        foreground.positionKm, background.positionKm) /
        (foregroundDistance * backgroundDistance);
    double separation = acos(SpaceIlluminationClamp(alignment, -1.0, 1.0));
    return SpaceIlluminationCircleCoverage(
        backgroundAngularRadius, foregroundAngularRadius, separation);
}
