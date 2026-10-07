#include "world/planet_landform_strategy.h"
#include "world/terrain.h"
#include "landform_assert.h"
#include "raymath.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static PlanetProfile Profile(uint32_t seed, double radius, float gravity,
                             float rotation, float ocean)
{
    return (PlanetProfile){ .seed = seed, .style = SOLAR_STYLE_TEMPERATE,
        .atmosphereType = PLANET_ATMOSPHERE_BREATHABLE, .hasSolidSurface = true,
        .physicalRadiusKm = radius, .surfaceGravity = gravity, .rotationRate = rotation,
        .equilibriumTempK = 288, .radiativeTempK = 255, .surfacePressureAtm = 1,
        .atmosphereDensity = 1, .receivedIrradiance = 1, .oceanCoverage = ocean,
        .iceCoverage = .1f, .terrainRoughness = .8f, .cloudCoverage = .4f,
        .yearLength = 365, .ageGyr = 4 };
}

static void ValidSample(LandformSample s)
{
    assert(isfinite(s.elevation) && isfinite(s.waterLevel));
    assert(isfinite(s.area) && s.area >= 0 && isfinite(s.slope) && s.slope >= 0);
    assert(isfinite(s.sedimentDepth) && s.sedimentDepth >= 0);
    assert(s.wetness >= 0 && s.wetness <= 1);
    if (s.hasWater) assert(floorf(s.waterLevel) > roundf(s.elevation));
    if (s.sedimentDepth > 0)
        assert(fabsf(s.gravel + s.sand + s.fines - 1) < 1e-5f);
}

static LandformSample SampleMap(const LandformAtlas *a, PlanetLandformStrategy *s,
                                int x, int z)
{
    SurfaceMapCell cell = SurfaceCanonicalMapCell(x, z);
    float base = TerrainPlanetBaselineElevation(s->seed, &s->profile, cell.x, cell.z, NULL);
    LandformSample result = LandformSampleAt(a,
        SurfaceDirectionFromMapCoordinates(cell.x, cell.z), base);
    ValidSample(result);
    return result;
}

static void TestAliases(const LandformAtlas *a, PlanetLandformStrategy *s)
{
    for (int z = -4096; z < 4096; z += 127) {
        LandformSample left = SampleMap(a, s, -8192, z);
        AssertLandformEqual(left, SampleMap(a, s, 8192, z));
        AssertLandformEqual(left, SampleMap(a, s, -24576, z));
    }
    for (int x = -8192; x < 8192; x += 257) {
        for (int offset = 0; offset < 8; offset++) {
            // Discrete pole reflection includes the one-column offset.
            AssertLandformEqual(SampleMap(a, s, x, 4096 + offset),
                SampleMap(a, s, x + 8192, 4095 - offset));
            AssertLandformEqual(SampleMap(a, s, x, -4097 - offset),
                SampleMap(a, s, x + 8192, -4096 + offset));
        }
    }
}

static void TestDrainage(const LandformAtlas *a)
{
    double area = 0, outlets = 0;
    unsigned mouths = 0, crossFace = 0;
    for (uint32_t i = 0; i < LANDFORM_CELL_COUNT; i++) {
        const LandformCell *c = &a->cells[i];
        area += c->localArea;
        assert(isfinite(c->spill) && c->spill >= c->elevation);
        assert(c->watershed < LANDFORM_CELL_COUNT);
        if (c->downstream == LANDFORM_NONE) { outlets += c->area; continue; }
        assert(c->downstream < LANDFORM_CELL_COUNT);
        const LandformCell *d = &a->cells[c->downstream];
        assert(d->order < c->order && d->spill <= c->spill);
        assert(d->area >= c->area && d->discharge >= c->discharge);
        assert(d->watershed == c->watershed);
        if (c->width > 0) {
            mouths += d->elevation <= a->seaLevel;
            crossFace += i / (LANDFORM_RESOLUTION * LANDFORM_RESOLUTION) !=
                c->downstream / (LANDFORM_RESOLUTION * LANDFORM_RESOLUTION);
        }
    }
    assert(fabs(outlets - area) < area * 1e-5);
    assert(mouths && crossFace);
    printf(" mouths=%u face_crossing_reaches=%u", mouths, crossFace);
}

static float Component(Vector3 p, int axis)
{
    return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}

static Vector3 WithComponent(Vector3 p, int axis, float value)
{
    if (axis == 0) p.x = value;
    else if (axis == 1) p.y = value;
    else p.z = value;
    return p;
}

static void TestFaceSeams(const LandformAtlas *a, PlanetLandformStrategy *s)
{
    float maxExtraCut = 0;
    unsigned pairs = 0;
    // Twelve geometric cube edges, sampled on both sides less than .01 block apart.
    // Account for integer baseline steps separately from the landform contribution.
    for (int first = 0; first < 3; first++) for (int second = first + 1; second < 3; second++)
    for (int signA = -1; signA <= 1; signA += 2) for (int signB = -1; signB <= 1; signB += 2)
    for (int t = -31; t <= 31; t++) {
        Vector3 edge = {0};
        edge = WithComponent(edge, first, signA);
        edge = WithComponent(edge, second, signB);
        edge = WithComponent(edge, 3 - first - second, t / 32.0f);
        Vector3 left = Vector3Normalize(WithComponent(edge, first, Component(edge, first) * 1.000001f));
        Vector3 right = Vector3Normalize(WithComponent(edge, second, Component(edge, second) * 1.000001f));
        float baseLeft = PlanetLandformBaseline(left, s).elevation;
        float baseRight = PlanetLandformBaseline(right, s).elevation;
        LandformSample l = LandformSampleAt(a, left, baseLeft);
        LandformSample r = LandformSampleAt(a, right, baseRight);
        ValidSample(l); ValidSample(r);
        float extraCut = fmaxf(0, fabsf(l.elevation - r.elevation) - fabsf(baseLeft - baseRight));
        maxExtraCut = fmaxf(maxExtraCut, extraCut);
        pairs++;
    }
    printf(" seam_pairs=%u seam_extra_cut_max=%.6f", pairs, maxExtraCut);
    // A sub-voxel chart change must not create a full-block terrain wall.
    assert(maxExtraCut < 1);
}

int main(void)
{
    const PlanetProfile profiles[] = {
        Profile(1234, 1500, 3, 0, .35f),
        Profile(4321, 5000, 7, 180, .60f),
        Profile(991, 11000, 18, -360, .85f)
    };
    for (unsigned variant = 0; variant < sizeof(profiles)/sizeof(*profiles); variant++) {
        PlanetLandformStrategy s = WaterWorldLandformStrategy(profiles[variant].seed, &profiles[variant]);
        assert(s.id == LANDFORM_STRATEGY_WATER_WORLD);
        LandformBuildInput input = PlanetLandformBuildInput(&s);
        LandformAtlas *a = LandformBuildWithInput(&input);
        assert(a && a->rivers && a->lakes);
        a->terraces = true;
        printf("planet seed=%u radius_km=%.0f gravity=%.1f rotation=%.1f ocean=%.2f rivers=%u lakes=%u",
            s.seed, s.profile.physicalRadiusKm, s.physics.gravity,
            s.profile.rotationRate, s.profile.oceanCoverage, a->rivers, a->lakes);
        TestDrainage(a);
        TestAliases(a, &s);
        TestFaceSeams(a, &s);
        unsigned wet = 0, sediment = 0;
        for (uint32_t i = 0; i < LANDFORM_CELL_COUNT; i += 37) {
            Vector3 p = a->cells[i].direction;
            LandformSample sample = LandformSampleAt(a, p, PlanetLandformBaseline(p, &s).elevation);
            ValidSample(sample);
            wet += sample.hasWater;
            sediment += sample.sedimentDepth > 0;
            LandformPhysicsDiagnostic diagnostic = LandformPhysicsDiagnose(
                a->physics, asin(Clamp(p.y, -1, 1)), 12, 3, 2, 1000);
            assert(diagnostic.valid && isfinite(diagnostic.deflectionBlocks));
            if (variant == 0) assert(diagnostic.deflectionBlocks == 0);
        }
        assert(wet && sediment);
        LandformAtlas *repeat = LandformBuildWithInput(&input);
        assert(repeat && !memcmp(a->cells, repeat->cells, sizeof(*a->cells) * LANDFORM_CELL_COUNT));
        printf(" wet_samples=%u sediment_samples=%u deterministic=yes\n", wet, sediment);
        LandformDestroy(repeat);
        LandformDestroy(a);
    }
    puts("planet landform quality tests passed");
}
