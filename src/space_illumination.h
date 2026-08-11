#ifndef VOXELCRAFT_SPACE_ILLUMINATION_H
#define VOXELCRAFT_SPACE_ILLUMINATION_H

typedef struct SpaceIlluminationVector3 {
    double x;
    double y;
    double z;
} SpaceIlluminationVector3;

typedef struct SpaceIlluminationBody {
    SpaceIlluminationVector3 positionKm;
    double radiusKm;
} SpaceIlluminationBody;

double SpaceIlluminationIrradianceEarth(double luminositySolar,
                                        double distanceKm);
double SpaceIlluminationOrbitMeanIrradianceEarth(
    double luminositySolar, double semiMajorAxisKm, double eccentricity);
double SpaceIlluminationCircleCoverage(double targetAngularRadius,
                                       double occulterAngularRadius,
                                       double angularSeparation);
double SpaceIlluminationOccultationFraction(
    SpaceIlluminationBody foreground, SpaceIlluminationBody background);

#endif
