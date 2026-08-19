#include "world/subsurface.h"

#include "world/surface_topology.h"

#include <math.h>

#define SUBSURFACE_PI 3.14159265358979323846f

static uint32_t SubsurfaceMix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

static float SubsurfacePhase(uint32_t seed, uint32_t lane)
{
    uint32_t value = SubsurfaceMix(seed ^ (lane * 0x9e3779b9u));
    return (float)(value & 0x00ffffffu) /
           16777215.0f * (2.0f * SUBSURFACE_PI);
}

static float SubsurfaceSmooth(float edge0, float edge1, float value)
{
    if (edge1 <= edge0) return value >= edge1 ? 1.0f : 0.0f;
    float t = (value - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static float SubsurfaceMax(float a, float b)
{
    return a > b ? a : b;
}

static Vector3 SubsurfacePlanetPosition(int x, int z)
{
    SurfaceMapCell cell = SurfaceCanonicalMapCell((float)x, (float)z);
    Vector3 direction = SurfaceDirectionFromMapCoordinates(
        (float)cell.x + 0.5f, (float)cell.z + 0.5f);
    return (Vector3){
        direction.x * SURFACE_RADIUS_BLOCKS,
        direction.y * SURFACE_RADIUS_BLOCKS,
        direction.z * SURFACE_RADIUS_BLOCKS
    };
}

static float SubsurfaceProjectedCoordinate(Vector3 position, Vector3 axis)
{
    return position.x * axis.x + position.y * axis.y +
           position.z * axis.z;
}

typedef struct SubsurfaceColumnCache {
    bool valid;
    int x;
    int z;
    uint32_t seed;
    float fx;
    float fz;
    float fw;
    float phase0;
    float phase1;
    float phase2;
    float phase3;
    float warpXBase;
    float warpZBase;
    float chamberBase;
    float shaftWindowOffset;
    float aquiferBase;
    int aquiferOffset;
} SubsurfaceColumnCache;

static _Thread_local SubsurfaceColumnCache subsurfaceColumnCache;

static const SubsurfaceColumnCache *SubsurfaceColumnAt(
    const SubsurfaceParams *params, int x, int z)
{
    SubsurfaceColumnCache *column = &subsurfaceColumnCache;
    if (column->valid && column->x == x && column->z == z &&
        column->seed == params->seed) {
        return column;
    }

    Vector3 planet = SubsurfacePlanetPosition(x, z);
    float fx = SubsurfaceProjectedCoordinate(
        planet, (Vector3){ 0.816497f, 0.408248f, 0.408248f });
    float fz = SubsurfaceProjectedCoordinate(
        planet, (Vector3){ -0.408248f, 0.816497f, 0.408248f });
    float fw = SubsurfaceProjectedCoordinate(
        planet, (Vector3){ -0.408248f, -0.408248f, 0.816497f });
    float phase0 = SubsurfacePhase(params->seed, 11u);
    float phase1 = SubsurfacePhase(params->seed, 23u);
    float phase2 = SubsurfacePhase(params->seed, 37u);
    float phase3 = SubsurfacePhase(params->seed, 53u);
    *column = (SubsurfaceColumnCache){
        .valid = true,
        .x = x,
        .z = z,
        .seed = params->seed,
        .fx = fx,
        .fz = fz,
        .fw = fw,
        .phase0 = phase0,
        .phase1 = phase1,
        .phase2 = phase2,
        .phase3 = phase3,
        .warpXBase = sinf(fz * 0.0103f + phase0) * 12.0f,
        .warpZBase = sinf(fw * 0.0091f + phase1) * 12.0f,
        .chamberBase =
            sinf(fx * 0.0107f + phase0) * 0.25f +
            sinf(fz * 0.0093f + phase1) * 0.24f +
            sinf(fw * 0.0111f + phase2) * 0.22f,
        .shaftWindowOffset = sinf(fx * 0.004f) * 0.7f,
        .aquiferBase =
            0.5f + sinf(fx * 0.0067f + phase1) * 0.20f +
            sinf(fz * 0.0079f + phase3) * 0.18f,
        .aquiferOffset = (int)lroundf(
            sinf(fx * 0.0051f + phase0) * 7.0f +
            sinf(fw * 0.0057f + phase2) * 5.0f)
    };
    return column;
}

SubsurfaceSample SubsurfaceSampleAt(const SubsurfaceParams *params,
                                    int x, int y, int z,
                                    int surfaceHeight)
{
    SubsurfaceSample sample = { 0 };
    if (!params || y < params->minY ||
        y >= surfaceHeight - params->surfaceClearance) {
        return sample;
    }

    float activity = params->activity;
    if (activity < 0.25f) activity = 0.25f;
    if (activity > 1.75f) activity = 1.75f;
    const SubsurfaceColumnCache *column = SubsurfaceColumnAt(params, x, z);
    float fx = column->fx;
    float fz = column->fz;
    float fw = column->fw;
    float fy = (float)y;

    float phase0 = column->phase0;
    float phase1 = column->phase1;
    float phase2 = column->phase2;
    float phase3 = column->phase3;
    float warpX = column->warpXBase +
                  sinf(fy * 0.0171f + phase1) * 5.0f;
    float warpZ = column->warpZBase -
                  sinf(fy * 0.0157f + phase0) * 5.0f;
    float tunnelA = sinf((fx + warpX) * 0.047f + fy * 0.019f + phase2);
    float tunnelB = sinf((fz + warpZ) * 0.043f - fy * 0.023f + phase3);
    float tunnelDistance = sqrtf(tunnelA * tunnelA + tunnelB * tunnelB);
    float tunnelRadius = 0.27f + (activity - 1.0f) * 0.07f +
        sinf((fx + fz + fw) * 0.0061f + fy * 0.0107f + phase0) * 0.045f;
    sample.tunnel = 1.0f - SubsurfaceSmooth(
        tunnelRadius, tunnelRadius + 0.22f, tunnelDistance);

    float chamberField = column->chamberBase +
        sinf(fy * 0.0179f + phase3) * 0.22f +
        sinf((fx - fz + fw * 0.43f) * 0.0053f +
             fy * 0.0081f + phase0) * 0.28f +
        sinf(fx * 0.0317f + fz * 0.0231f + fw * 0.0197f +
             fy * 0.0131f + phase2) * 0.20f;
    chamberField = 0.5f + chamberField * 0.5f;
    float chamberStart = 0.50f - (activity - 1.0f) * 0.08f;
    sample.chamber = SubsurfaceSmooth(chamberStart, chamberStart + 0.18f,
                                      chamberField);

    float shaftWarp = sinf(fy * 0.0109f + phase3) * 0.12f;
    float shaftA = sinf(fx * 0.0209f + phase0 + shaftWarp);
    float shaftB = sinf(fw * 0.0193f + phase2 - shaftWarp);
    float shaftDistance = sqrtf(shaftA * shaftA + shaftB * shaftB);
    sample.shaft = (1.0f - SubsurfaceSmooth(
        0.075f, 0.20f + activity * 0.025f, shaftDistance)) *
        SubsurfaceSmooth(10.0f, 28.0f,
                         (float)(surfaceHeight - y));
    float shaftWindow = 0.5f + 0.5f *
        sinf(fy * 0.051f + phase1 + column->shaftWindowOffset);
    sample.shaft *= SubsurfaceSmooth(0.42f, 0.68f, shaftWindow);

    sample.openness = SubsurfaceMax(
        sample.tunnel,
        SubsurfaceMax(sample.chamber, sample.shaft * 0.92f));
    sample.cave = sample.openness >= 0.52f;

    float aquiferField = column->aquiferBase +
        sinf((fx + fw) * 0.0041f + fy * 0.0113f + phase2) * 0.16f;
    if (aquiferField < 0.0f) aquiferField = 0.0f;
    if (aquiferField > 1.0f) aquiferField = 1.0f;
    sample.aquifer = aquiferField;
    int localAquiferLevel = params->aquiferLevel + column->aquiferOffset;
    float aquiferChance = params->aquiferChance;
    if (aquiferChance < 0.0f) aquiferChance = 0.0f;
    if (aquiferChance > 1.0f) aquiferChance = 1.0f;
    float wetThreshold = 0.86f - aquiferChance * 0.58f;
    sample.flooded = sample.cave && y <= localAquiferLevel &&
                     sample.aquifer >= wetThreshold;
    return sample;
}
