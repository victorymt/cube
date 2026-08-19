#include "world/terrain.h"

#include "world/subsurface.h"
#include "world/surface_topology.h"
#include "world/terrain_geology_internal.h"
#include "world/terrain_home_materials_internal.h"
#include "world/terrain_structures_internal.h"
#include "world/home_tree_shape.h"

#include "ecology/ecology.h"
#include "ecology/flora_taxa.h"

#include "raymath.h"
#include "world/chunks.h"
#include "space/space_state.h"
#include "world/world.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "world/chunks.h"
#include "world/world.h"

static PlanetChunkDecorator planetChunkDecorator;

void TerrainInstallPlanetChunkDecorator(PlanetChunkDecorator decorator)
{
    planetChunkDecorator = decorator;
}

static unsigned int Hash2DBits(unsigned int xBits, unsigned int zBits)
{
    unsigned int h = 2166136261u;
    h = (h ^ xBits) * 16777619u;
    h = (h ^ (zBits * 374761393u)) * 16777619u;
    h ^= h >> 13;
    h *= 1274126177u;
    return h ^ (h >> 16);
}

unsigned int Hash2D(int x, int z)
{
    return Hash2DBits((unsigned int)x, (unsigned int)z);
}

static unsigned int MixWorldSeed(unsigned int hash)
{
    uint32_t seed = WorldGetSeed();
    if (seed == DEFAULT_WORLD_SEED) return hash;
    hash ^= seed + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    hash ^= hash >> 16;
    hash *= 2246822519u;
    return hash ^ (hash >> 13);
}

static float WorldSeedCoordinateOffset(unsigned int lane)
{
    uint32_t seed = WorldGetSeed();
    if (seed == DEFAULT_WORLD_SEED) return 0.0f;
    unsigned int hash = seed ^ (lane * 0x9e3779b9u);
    hash ^= hash >> 16;
    hash *= 2246822519u;
    int centered = (int)(hash % 200001u) - 100000;
    return (float)centered * 0.01f;
}

unsigned int WorldHash2D(int x, int z)
{
    return WorldHash2DBits((unsigned int)x, (unsigned int)z);
}

unsigned int WorldHash2DBits(unsigned int xBits, unsigned int zBits)
{
    return MixWorldSeed(Hash2DBits(xBits, zBits));
}

unsigned int WorldHash3D(int x, int y, int z)
{
    return MixWorldSeed(Hash3D(x, y, z));
}

static float WorldHashUnit2D(int x, int z, unsigned int lane)
{
    unsigned int h = WorldHash2DBits((unsigned int)(x + (int)(lane * 101u)),
                                     (unsigned int)(z - (int)(lane * 173u)));
    return (float)(h & 0x00ffffffu) / 16777215.0f;
}

static float NoiseSmooth(float value)
{
    return value * value * (3.0f - 2.0f * value);
}

static float WorldValueNoise2D(float x, float z, unsigned int lane)
{
    int x0 = (int)floorf(x);
    int z0 = (int)floorf(z);
    float tx = NoiseSmooth(x - (float)x0);
    float tz = NoiseSmooth(z - (float)z0);
    float a = Lerp(WorldHashUnit2D(x0, z0, lane),
                   WorldHashUnit2D(x0 + 1, z0, lane), tx);
    float b = Lerp(WorldHashUnit2D(x0, z0 + 1, lane),
                   WorldHashUnit2D(x0 + 1, z0 + 1, lane), tx);
    return Lerp(a, b, tz);
}

static float WorldFractalNoise2D(float x, float z, unsigned int lane)
{
    float value = 0.0f;
    float amplitude = 0.58f;
    float total = 0.0f;
    for (int octave = 0; octave < 5; octave++) {
        value += WorldValueNoise2D(x, z, lane + (unsigned int)octave * 17u) *
                 amplitude;
        total += amplitude;
        x = x * 2.03f + 11.3f;
        z = z * 2.03f - 7.9f;
        amplitude *= 0.49f;
    }
    return value / total;
}

static float SmoothRange(float low, float high, float value)
{
    if (value <= low) return 0.0f;
    if (value >= high) return 1.0f;
    return NoiseSmooth((value - low) / (high - low));
}

static void BathymetryClassify(BathymetrySample *sample,
                               float trenchRelief, float seamountRelief)
{
    if (!sample || sample->seaLevel < 0 || sample->waterDepth <= 0) return;
    int depth = sample->waterDepth;
    if (seamountRelief >= 4.0f && depth >= 8) {
        sample->zone = BATHYMETRY_ZONE_SEAMOUNT;
    } else if (trenchRelief >= 4.0f && depth >= 48) {
        sample->zone = BATHYMETRY_ZONE_TRENCH;
    } else if (depth <= 6) {
        sample->zone = BATHYMETRY_ZONE_COAST;
    } else if (depth <= 20) {
        sample->zone = BATHYMETRY_ZONE_SHELF;
    } else if (depth <= 45) {
        sample->zone = BATHYMETRY_ZONE_SLOPE;
    } else {
        sample->zone = BATHYMETRY_ZONE_ABYSSAL_PLAIN;
    }

    if (depth <= 20) {
        sample->material = BATHYMETRY_MATERIAL_SAND;
    } else if (depth <= 45 || sample->zone == BATHYMETRY_ZONE_SEAMOUNT) {
        sample->material = BATHYMETRY_MATERIAL_SEDIMENT;
    } else {
        sample->material = BATHYMETRY_MATERIAL_ROCK;
    }
}

static BathymetrySample BathymetryFromSignals(int seaLevel, float ocean,
                                              float trench, float seamount,
                                              float detail)
{
    BathymetrySample sample = {
        .seaLevel = seaLevel,
        .seabedY = seaLevel,
        .zone = BATHYMETRY_ZONE_LAND,
        .material = BATHYMETRY_MATERIAL_ROCK
    };
    if (seaLevel < 0 || ocean <= 0.0f) return sample;

    ocean = Clamp(ocean, 0.0f, 1.0f);
    trench = Clamp(trench, 0.0f, 1.0f);
    seamount = Clamp(seamount, 0.0f, 1.0f);
    float deepOcean = SmoothRange(0.48f, 0.92f, ocean);
    float trenchRelief = trench * SmoothRange(0.52f, 0.82f, ocean) * 12.0f;
    float seamountRelief = seamount * deepOcean * 22.0f;
    float depth = 1.0f;
    depth += SmoothRange(0.00f, 0.16f, ocean) * 5.0f;
    depth += SmoothRange(0.10f, 0.36f, ocean) * 14.0f;
    depth += SmoothRange(0.30f, 0.67f, ocean) * 24.0f;
    depth += SmoothRange(0.58f, 0.92f, ocean) * 16.0f;
    depth += trenchRelief;
    depth -= seamountRelief;
    depth += Clamp(detail, -0.5f, 0.5f) * 4.0f;
    depth = Clamp(depth, 1.0f, (float)BATHYMETRY_MAX_WATER_DEPTH);

    sample.waterDepth = (int)lroundf(depth);
    sample.seabedY = seaLevel - sample.waterDepth;
    if (sample.seabedY < BATHYMETRY_MIN_SEABED_Y) {
        sample.seabedY = BATHYMETRY_MIN_SEABED_Y;
        sample.waterDepth = seaLevel - sample.seabedY;
    }
    BathymetryClassify(&sample, trenchRelief, seamountRelief);
    return sample;
}

static void HomeBathymetryClassify(BathymetrySample *sample,
                                   float trenchRelief,
                                   float seamountRelief)
{
    if (!sample || sample->waterDepth <= 0) return;
    int depth = sample->waterDepth;
    if (seamountRelief >= 600.0f && depth >= 200) {
        sample->zone = BATHYMETRY_ZONE_SEAMOUNT;
    } else if (trenchRelief >= 900.0f && depth >= 6000) {
        sample->zone = BATHYMETRY_ZONE_TRENCH;
    } else if (depth <= 50) {
        sample->zone = BATHYMETRY_ZONE_COAST;
    } else if (depth <= 200) {
        sample->zone = BATHYMETRY_ZONE_SHELF;
    } else if (depth <= 3000) {
        sample->zone = BATHYMETRY_ZONE_SLOPE;
    } else {
        sample->zone = BATHYMETRY_ZONE_ABYSSAL_PLAIN;
    }

    if (depth <= 200) {
        sample->material = BATHYMETRY_MATERIAL_SAND;
    } else if (depth <= 6000 || sample->zone == BATHYMETRY_ZONE_SEAMOUNT) {
        sample->material = BATHYMETRY_MATERIAL_SEDIMENT;
    } else {
        sample->material = BATHYMETRY_MATERIAL_ROCK;
    }
}

static BathymetrySample HomeBathymetryFromSignals(
    float ocean, float trench, float seamount, float detail)
{
    BathymetrySample sample = {
        .seaLevel = HOME_SEA_LEVEL,
        .seabedY = HOME_SEA_LEVEL,
        .zone = BATHYMETRY_ZONE_LAND,
        .material = BATHYMETRY_MATERIAL_ROCK
    };
    if (ocean <= 0.0f) return sample;

    ocean = Clamp(ocean, 0.0f, 1.0f);
    trench = Clamp(trench, 0.0f, 1.0f);
    seamount = Clamp(seamount, 0.0f, 1.0f);
    float deepOcean = SmoothRange(0.48f, 0.92f, ocean);
    float trenchRelief =
        trench * SmoothRange(0.52f, 0.82f, ocean) * 6500.0f;
    float seamountRelief = seamount * deepOcean * 2600.0f;
    float depth = 8.0f;
    depth += SmoothRange(0.00f, 0.16f, ocean) * 42.0f;
    depth += SmoothRange(0.10f, 0.36f, ocean) * 150.0f;
    depth += SmoothRange(0.30f, 0.67f, ocean) * 2800.0f;
    depth += SmoothRange(0.58f, 0.92f, ocean) * 3900.0f;
    depth += trenchRelief;
    depth -= seamountRelief;
    depth += Clamp(detail, -0.5f, 0.5f) * 200.0f;
    depth = Clamp(depth, 1.0f,
                  (float)HOME_BATHYMETRY_MAX_WATER_DEPTH);

    sample.waterDepth = (int)lroundf(depth);
    sample.seabedY = HOME_SEA_LEVEL - sample.waterDepth;
    if (sample.seabedY < HOME_BATHYMETRY_MIN_SEABED_Y) {
        sample.seabedY = HOME_BATHYMETRY_MIN_SEABED_Y;
        sample.waterDepth = HOME_SEA_LEVEL - sample.seabedY;
    }
    HomeBathymetryClassify(&sample, trenchRelief, seamountRelief);
    return sample;
}

static BathymetrySample BathymetryForHeight(int seaLevel, int height,
                                            float trench, float seamount)
{
    BathymetrySample sample = {
        .seaLevel = seaLevel,
        .seabedY = height,
        .waterDepth = seaLevel >= 0 && height < seaLevel ? seaLevel - height : 0,
        .zone = BATHYMETRY_ZONE_LAND,
        .material = BATHYMETRY_MATERIAL_ROCK
    };
    BathymetryClassify(&sample, trench * 12.0f, seamount * 22.0f);
    return sample;
}

const char *BathymetryZoneName(BathymetryZone zone)
{
    switch (zone) {
    case BATHYMETRY_ZONE_COAST: return "coast";
    case BATHYMETRY_ZONE_SHELF: return "shelf";
    case BATHYMETRY_ZONE_SLOPE: return "slope";
    case BATHYMETRY_ZONE_ABYSSAL_PLAIN: return "abyssal_plain";
    case BATHYMETRY_ZONE_TRENCH: return "trench";
    case BATHYMETRY_ZONE_SEAMOUNT: return "seamount";
    case BATHYMETRY_ZONE_LAND:
    default: return "land";
    }
}

const char *BathymetryMaterialName(BathymetryMaterial material)
{
    switch (material) {
    case BATHYMETRY_MATERIAL_SAND: return "sand";
    case BATHYMETRY_MATERIAL_SEDIMENT: return "sediment";
    case BATHYMETRY_MATERIAL_ROCK:
    default: return "rock";
    }
}

BlockType BathymetryMaterialBlock(BathymetryMaterial material)
{
    switch (material) {
    case BATHYMETRY_MATERIAL_SAND: return BLOCK_SAND;
    case BATHYMETRY_MATERIAL_SEDIMENT: return BLOCK_MUD;
    case BATHYMETRY_MATERIAL_ROCK:
    default: return BLOCK_GRAVEL;
    }
}

static float HomeTerrainElevationCore(int x, int z,
                                      SurfaceTerrainSample *sample)
{
    float fx = (float)x + WorldSeedCoordinateOffset(1u);
    float fz = (float)z + WorldSeedCoordinateOffset(2u);
    float continentalness = WorldFractalNoise2D(fx * 0.00135f,
                                                fz * 0.00135f, 21u);
    float erosion = WorldFractalNoise2D(fx * 0.0038f, fz * 0.0038f, 79u);
    float ridgeNoise = WorldFractalNoise2D(fx * 0.0026f, fz * 0.0026f, 131u);
    float ridge = 1.0f - fabsf(ridgeNoise * 2.0f - 1.0f);
    ridge = SmoothRange(0.56f, 0.94f, ridge);
    float peakNoise = WorldFractalNoise2D(fx * 0.0065f, fz * 0.0065f, 211u);
    float peak = SmoothRange(0.69f, 0.91f, peakNoise) * ridge;
    float trenchBoundary = 1.0f - fabsf(
        WorldFractalNoise2D(fx * 0.0019f, fz * 0.0019f, 307u) * 2.0f - 1.0f);
    float trench = SmoothRange(0.80f, 0.97f, trenchBoundary);
    float seamount = SmoothRange(
        0.62f, 0.90f,
        WorldFractalNoise2D(fx * 0.00105f, fz * 0.00105f, 353u));
    float detail = WorldFractalNoise2D(fx * 0.021f, fz * 0.021f, 401u) - 0.5f;
    const float coast = 0.50f;
    float elevation = 0.0f;
    BathymetrySample bathymetry = BathymetryForHeight(
        HOME_SEA_LEVEL, HOME_SEA_LEVEL, 0.0f, 0.0f);
    if (continentalness < coast) {
        float ocean = Clamp((coast - continentalness) / coast, 0.0f, 1.0f);
        bathymetry = HomeBathymetryFromSignals(
            ocean, trench, seamount, detail);
        elevation = (float)bathymetry.seabedY;
    } else {
        float land = Clamp((continentalness - coast) / (1.0f - coast),
                           0.0f, 1.0f);
        float mountainMask = SmoothRange(0.08f, 0.64f, land) *
                             (1.0f - erosion * 0.48f);
        elevation = (float)HOME_SEA_LEVEL + 3.0f + land * 34.0f;
        elevation += ridge * mountainMask * 92.0f;
        elevation += peak * mountainMask * 48.0f;
        elevation += detail * (5.0f + land * 8.0f);
        elevation = Clamp(elevation, 8.0f, 240.0f);
        bathymetry.seabedY = (int)lroundf(elevation);
    }
    if (sample) {
        sample->continentalness = continentalness;
        sample->erosion = erosion;
        sample->ridge = ridge;
        sample->peak = peak;
        sample->trench = trench;
        sample->bathymetry = bathymetry;
    }
    return elevation;
}

SurfaceTerrainSample SurfaceTerrainAt(int x, int z, TerrainMode mode)
{
    SurfaceTerrainSample sample = { 0 };
    sample.seaLevel = mode == TERRAIN_FLAT ? -1.0f : (float)HOME_SEA_LEVEL;
    if (mode == TERRAIN_FLAT) {
        sample.elevation = 8.0f;
        sample.continentalness = 1.0f;
        sample.biome = BIOME_PLAINS;
        sample.bathymetry = (BathymetrySample){
            .seaLevel = -1,
            .seabedY = 8,
            .waterDepth = 0,
            .zone = BATHYMETRY_ZONE_LAND,
            .material = BATHYMETRY_MATERIAL_ROCK
        };
        return sample;
    }
    sample.elevation = HomeTerrainElevationCore(x, z, &sample);
    float east = HomeTerrainElevationCore(x + 1, z, NULL);
    float west = HomeTerrainElevationCore(x - 1, z, NULL);
    float north = HomeTerrainElevationCore(x, z - 1, NULL);
    float south = HomeTerrainElevationCore(x, z + 1, NULL);
    sample.slope = fmaxf(fabsf(east - west), fabsf(north - south)) * 0.5f;
    float climate = WorldFractalNoise2D(
        ((float)x + WorldSeedCoordinateOffset(3u)) * 0.0018f,
        ((float)z + WorldSeedCoordinateOffset(4u)) * 0.0018f, 503u);
    float wetness = WorldFractalNoise2D(
        ((float)x + WorldSeedCoordinateOffset(5u)) * 0.0024f,
        ((float)z + WorldSeedCoordinateOffset(6u)) * 0.0024f, 607u);
    if (sample.elevation >= 132.0f || sample.ridge > 0.58f) {
        sample.biome = BIOME_MOUNTAIN;
    } else if (climate < 0.25f) {
        sample.biome = BIOME_SNOW;
    } else if (climate < 0.43f) {
        sample.biome = BIOME_DESERT;
    } else if (sample.bathymetry.waterDepth == 0 &&
               sample.elevation <= sample.seaLevel + 12.0f &&
               sample.slope <= 2.4f && wetness > 0.55f) {
        sample.biome = BIOME_SWAMP;
    } else if (climate > 0.64f) {
        sample.biome = BIOME_FOREST;
    } else {
        sample.biome = BIOME_PLAINS;
    }
    return sample;
}

float TerrainNoise(float x, float z)
{
    return HomeTerrainElevationCore((int)floorf(x), (int)floorf(z), NULL) -
           (float)HOME_SEA_LEVEL;
}

float BiomeNoise(int x, int z)
{
    SurfaceTerrainSample sample = SurfaceTerrainAt(x, z, TERRAIN_VARIED);
    return sample.continentalness * 2.0f - 1.0f;
}

Biome BiomeAt(int x, int z)
{
    return SurfaceTerrainAt(x, z, TERRAIN_VARIED).biome;
}

int TerrainSeaLevel(TerrainMode mode)
{
    return mode == TERRAIN_FLAT ? -1 : HOME_SEA_LEVEL;
}

int TerrainHeight(int x, int z, TerrainMode mode)
{
    if (mode == TERRAIN_FLAT) return 8;
    return (int)lroundf(HomeTerrainElevationCore(x, z, NULL));
}

BathymetrySample TerrainBathymetryAt(int x, int z, TerrainMode mode)
{
    return SurfaceTerrainAt(x, z, mode).bathymetry;
}

enum {
    HOME_TREE_MIN_SPACING_RADIUS = 3,
    HOME_TREE_MAX_CROWN_RADIUS = 4,
    HOME_TREE_BROADLEAF_VARIANT_COUNT = 3,
    HOME_TREE_CONIFER_VARIANT_COUNT = 2
};

static int HomeTreeDensityDivisor(Biome biome)
{
    switch (biome) {
    case BIOME_FOREST:   return 47;
    case BIOME_PLAINS:   return 131;
    case BIOME_MOUNTAIN: return 149;
    case BIOME_SNOW:     return 67;
    case BIOME_SWAMP:    return 211;
    default:             return 0;
    }
}

static unsigned int HomeTreeShapeHash(int treeX, int treeZ)
{
    return WorldHash2DBits((unsigned int)treeX ^ 0xa511e9b3u,
                           (unsigned int)treeZ ^ 0x63d83595u);
}

#ifdef TERRAIN_TESTING
static FloraTaxonId HomeTreeTaxonForFamily(int treeX, int treeZ,
                                           bool conifer)
{
    unsigned int roll = HomeTreeShapeHash(treeX, treeZ) % 100u;
    if (conifer) {
        return roll < 62u ? FLORA_TAXON_SPRUCE : FLORA_TAXON_PINE;
    }
    if (roll < 45u) return FLORA_TAXON_OAK;
    if (roll < 75u) return FLORA_TAXON_WILLOW;
    return FLORA_TAXON_BIRCH;
}
#endif

static FloraHabitat HomeFloraHabitatAt(
    int x, int z, TerrainMode mode, const SurfaceTerrainSample *knownSurface)
{
    SurfaceTerrainSample surface = knownSurface ? *knownSurface :
        SurfaceTerrainAt(x, z, mode);
    float temperatureK = 288.0f;
    float moisture = 0.55f;
    float usableLight = 0.76f;
    switch (surface.biome) {
    case BIOME_FOREST:
        temperatureK = 285.0f;
        moisture = 0.78f;
        usableLight = 0.54f;
        break;
    case BIOME_DESERT:
        temperatureK = 306.0f;
        moisture = 0.10f;
        usableLight = 0.94f;
        break;
    case BIOME_SNOW:
        temperatureK = 263.0f;
        moisture = 0.48f;
        usableLight = 0.62f;
        break;
    case BIOME_MOUNTAIN:
        temperatureK = 275.0f;
        moisture = 0.42f;
        usableLight = 0.82f;
        break;
    case BIOME_SWAMP:
        temperatureK = 286.0f;
        moisture = 0.94f;
        usableLight = 0.64f;
        break;
    case BIOME_PLAINS:
    default:
        break;
    }
    float elevation = fmaxf(surface.elevation - surface.seaLevel, 0.0f);
    temperatureK -= elevation * 0.0065f;
    int groundY = (int)lroundf(surface.elevation);
    BlockType substrate = TerrainHomeBaseBlockFromSample(
        x, groundY, z, mode, &surface, TerrainSeaLevel(mode));
    return (FloraHabitat){
        .temperatureK = temperatureK,
        .moisture = moisture,
        .usableLight = usableLight,
        .elevation = elevation,
        .slope = Clamp(surface.slope / 8.0f, 0.0f, 1.0f),
        .biome = surface.biome,
        .substrate = substrate,
        .burnSeverity = 0.0f,
        .burnRecovery = 1.0f
    };
}

FloraHabitat TerrainHomeFloraHabitatAt(int x, int z, TerrainMode mode)
{
    return HomeFloraHabitatAt(x, z, mode, NULL);
}

static FloraTaxonId HomeTreeTaxonAt(
    int treeX, int treeZ, TerrainMode mode,
    const SurfaceTerrainSample *knownSurface)
{
    FloraHabitat habitat = HomeFloraHabitatAt(
        treeX, treeZ, mode, knownSurface);
    return FloraSelectTaxon(&habitat,
                            HomeTreeShapeHash(treeX, treeZ), true);
}

FloraTaxonId TerrainHomeTreeTaxonAt(int x, int z, TerrainMode mode)
{
    return HomeTreeTaxonAt(x, z, mode, NULL);
}

static int HomeTreeCrownRadius(FloraTaxonId taxonId)
{
    const FloraTaxon *taxon = FloraTaxonAt(taxonId);
    if (!taxon || !FloraTaxonIsTree(taxonId)) {
        return HOME_TREE_MIN_SPACING_RADIUS;
    }
    return (int)lroundf(taxon->crownRadius);
}

static bool HomeTreeCandidateAt(int x, int z, TerrainMode mode,
                                FloraTaxonId *outTaxon)
{
    if (mode == TERRAIN_FLAT) return false;

    SurfaceTerrainSample surface = SurfaceTerrainAt(x, z, mode);
    int divisor = HomeTreeDensityDivisor(surface.biome);
    if (divisor == 0 || WorldHash2D(x, z) % (unsigned int)divisor != 0u) {
        return false;
    }
    int height = (int)lroundf(surface.elevation);
    if (height <= (int)lroundf(surface.seaLevel) ||
        height > SURFACE_MAX_Y_EXCLUSIVE - 18) {
        return false;
    }
    FloraTaxonId taxon = HomeTreeTaxonAt(x, z, mode, &surface);
    if (taxon == FLORA_TAXON_COUNT) return false;
    if (outTaxon) *outTaxon = taxon;
    return true;
}

static unsigned int HomeTreePlacementPriority(int x, int z)
{
    return WorldHash2DBits((unsigned int)x ^ 0x68bc21ebu,
                           (unsigned int)z ^ 0x02e5be93u);
}

bool ShouldPlaceTree(int x, int z, TerrainMode mode)
{
    FloraTaxonId taxon = FLORA_TAXON_COUNT;
    if (!HomeTreeCandidateAt(x, z, mode, &taxon)) return false;

    unsigned int priority = HomeTreePlacementPriority(x, z);
    int crownRadius = HomeTreeCrownRadius(taxon);
    for (int dx = -HOME_TREE_MAX_CROWN_RADIUS;
         dx <= HOME_TREE_MAX_CROWN_RADIUS; dx++) {
        for (int dz = -HOME_TREE_MAX_CROWN_RADIUS;
             dz <= HOME_TREE_MAX_CROWN_RADIUS; dz++) {
            if (dx == 0 && dz == 0) continue;
            int neighborX = x + dx;
            int neighborZ = z + dz;
            FloraTaxonId neighborTaxon = FLORA_TAXON_COUNT;
            if (!HomeTreeCandidateAt(neighborX, neighborZ, mode,
                                     &neighborTaxon)) {
                continue;
            }
            int neighborCrownRadius = HomeTreeCrownRadius(
                neighborTaxon);
            int requiredSpacing = crownRadius > neighborCrownRadius
                ? crownRadius : neighborCrownRadius;
            if (requiredSpacing < HOME_TREE_MIN_SPACING_RADIUS) {
                requiredSpacing = HOME_TREE_MIN_SPACING_RADIUS;
            }
            int distance = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
            if (distance > requiredSpacing) continue;
            unsigned int neighborPriority =
                HomeTreePlacementPriority(neighborX, neighborZ);
            if (neighborPriority < priority ||
                (neighborPriority == priority &&
                 (neighborX < x ||
                  (neighborX == x && neighborZ < z)))) {
                return false;
            }
        }
    }
    return true;
}

static SubsurfaceParams HomeSubsurfaceParams(void)
{
    return (SubsurfaceParams){
        .seed = WorldGetSeed(),
        .activity = 1.0f,
        .minY = 2,
        .surfaceClearance = 4,
        .aquiferLevel = 36,
        .aquiferChance = 0.68f
    };
}

bool CaveAt(int x, int y, int z, int height)
{
    SubsurfaceParams params = HomeSubsurfaceParams();
    return SubsurfaceSampleAt(&params, x, y, z, height).cave;
}

bool CaveWaterAt(int x, int y, int z, int height)
{
    SubsurfaceParams params = HomeSubsurfaceParams();
    return SubsurfaceSampleAt(&params, x, y, z, height).flooded;
}

BlockType OreAt(int x, int y, int z)
{
    unsigned int h = WorldHash3D(x, y, z);
    if (y <= 11 && (h % 281u) == 0u) return BLOCK_DIAMOND_ORE;
    if (y <= 16 && (h % 149u) == 0u) return BLOCK_GOLD_ORE;
    if (y <= 24 && (h % 223u) == 0u) return BLOCK_SILVER_ORE;
    if (y <= 26 && (h % 71u) == 0u) return BLOCK_IRON_ORE;
    if (y <= 36 && (h % 181u) == 0u) return BLOCK_NICKEL_ORE;
    if (y <= 42 && (h % 59u) == 0u) return BLOCK_COPPER_ORE;
    if (y <= 52 && (h % 127u) == 0u) return BLOCK_TIN_ORE;
    if (y <= 30 && (h % 43u) == 0u) return BLOCK_COAL_ORE;
    if (y <= 30 && (h % 193u) == 0u) return BLOCK_MAGNETITE_ORE;
    if (y <= 46 && (h % 167u) == 0u) return BLOCK_HEMATITE_ORE;
    unsigned int quartz = WorldHash3D(
        FloorDivInt(x, 3) + 719, FloorDivInt(y, 2) - 431,
        FloorDivInt(z, 3) + 283);
    if (y <= 96 && quartz % 113u == 0u) return BLOCK_QUARTZ_ORE;
    return BLOCK_STONE;
}

BlockType StoneOrCaveBlock(int x, int y, int z, int height)
{
    SubsurfaceParams params = HomeSubsurfaceParams();
    SubsurfaceSample cave = SubsurfaceSampleAt(&params, x, y, z, height);
    if (cave.cave) return cave.flooded ? BLOCK_WATER : BLOCK_AIR;
    return OreAt(x, y, z);
}

bool ShouldPlacePond(int x, int z, int height)
{
    Biome biome = BiomeAt(x, z);
    if (biome == BIOME_DESERT || biome == BIOME_MOUNTAIN) return false;
    if (biome == BIOME_SWAMP) {
        return height <= HOME_SEA_LEVEL + 12 &&
               WorldHash2D(x, z) % 13u == 0u;
    }
    return height <= HOME_SEA_LEVEL + 4 &&
           WorldHash2D(x, z) % 97u == 0u;
}

void SetChunkLocalBlock(Chunk *chunk, int worldX, int y, int worldZ, BlockType type)
{
    if (!InHeight(y)) return;

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(worldX, worldZ, &cx, &cz, &lx, &lz);
    if (chunk->cx == cx && chunk->cz == cz) {
        ChunkSetLocalBlock(chunk, lx, y, lz, type);
    }
}

static bool PlaceHomeTreeBlock(void *context, int worldX, int y, int worldZ,
                               BlockType type, bool replace)
{
    Chunk *chunk = context;
    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(worldX, worldZ, &cx, &cz, &lx, &lz);
    if (chunk->cx == cx && chunk->cz == cz &&
        (replace || ChunkGetLocalBlock(chunk, lx, y, lz) == BLOCK_AIR)) {
        ChunkSetLocalBlock(chunk, lx, y, lz, type);
    }
    return true;
}

static bool HomeTreeShapeSpecAt(int treeX, int base, int treeZ,
                                FloraTaxonId taxonId,
                                HomeTreeShapeSpec *outSpec)
{
    const FloraTaxon *taxon = FloraTaxonAt(taxonId);
    if (!outSpec || !taxon || !FloraTaxonIsTree(taxonId)) return false;
    *outSpec = (HomeTreeShapeSpec){
        .rootX = treeX,
        .baseY = base,
        .rootZ = treeZ,
        .taxonId = taxonId,
        .shapeHash = HomeTreeShapeHash(treeX, treeZ),
        .primaryBlock = taxon->primaryBlock,
        .accentBlock = taxon->accentBlock
    };
    return true;
}

static void HomeTreeDirection(int direction, int *outX, int *outZ)
{
    static const int directions[4][2] = {
        { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }
    };
    int index = direction & 3;
    *outX = directions[index][0];
    *outZ = directions[index][1];
}

static void PlaceHomeTree(Chunk *chunk, int treeX, int base, int treeZ,
                          FloraTaxonId taxonId)
{
    const FloraTaxon *taxon = FloraTaxonAt(taxonId);
    if (!taxon || !FloraTaxonIsTree(taxonId)) return;
    HomeTreeShapeSpec spec = { 0 };
    HomeTreeShapeBounds bounds = { 0 };
    if (!HomeTreeShapeSpecAt(treeX, base, treeZ, taxonId, &spec) ||
        !HomeTreeShapeBoundsAt(&spec, &bounds)) return;

    int chunkMinX = chunk->cx * CHUNK_SIZE;
    int chunkMinZ = chunk->cz * CHUNK_SIZE;
    int chunkMaxX = chunkMinX + CHUNK_SIZE - 1;
    int chunkMaxZ = chunkMinZ + CHUNK_SIZE - 1;
    bool intersects = bounds.maxX >= chunkMinX && bounds.minX <= chunkMaxX &&
                      bounds.maxZ >= chunkMinZ && bounds.minZ <= chunkMaxZ;
    if (!intersects) return;

    if (chunk->floraStructureCount >= MAX_CHUNK_FLORA_STRUCTURES) return;
    chunk->floraStructures[chunk->floraStructureCount++] =
        (FloraStructureInstance){
            .kind = FLORA_STRUCTURE_HOME_TREE,
            .taxonId = taxonId,
            .shapeHash = spec.shapeHash,
            .rootX = treeX,
            .groundY = base - 1,
            .rootZ = treeZ,
            .minX = bounds.minX,
            .minY = bounds.minY,
            .minZ = bounds.minZ,
            .maxX = bounds.maxX,
            .maxY = bounds.maxY,
            .maxZ = bounds.maxZ,
            .primaryBlock = taxon->primaryBlock,
            .accentBlock = taxon->accentBlock,
            .windResponse = taxon->windResponse
        };
    HomeTreeShapeEmit(&spec, PlaceHomeTreeBlock, chunk);
}

#ifdef TERRAIN_TESTING
int TerrainTestHomeTreeVariantAt(int treeX, int treeZ, bool conifer)
{
    FloraTaxonId taxon = HomeTreeTaxonForFamily(treeX, treeZ, conifer);
    if (conifer) return taxon == FLORA_TAXON_SPRUCE ? 0 : 1;
    if (taxon == FLORA_TAXON_OAK) return 0;
    return taxon == FLORA_TAXON_WILLOW ? 1 : 2;
}

int TerrainTestHomeTreeCrownRadiusAt(int treeX, int treeZ)
{
    return HomeTreeCrownRadius(HomeTreeTaxonAt(
        treeX, treeZ, TERRAIN_VARIED, NULL));
}

void TerrainTestPlaceHomeTree(Chunk *chunk, int treeX, int base, int treeZ,
                              bool conifer, int variant)
{
    int count = conifer ? HOME_TREE_CONIFER_VARIANT_COUNT
                        : HOME_TREE_BROADLEAF_VARIANT_COUNT;
    int normalized = variant % count;
    if (normalized < 0) normalized += count;
    static const FloraTaxonId broadleaf[] = {
        FLORA_TAXON_OAK, FLORA_TAXON_WILLOW, FLORA_TAXON_BIRCH
    };
    static const FloraTaxonId conifers[] = {
        FLORA_TAXON_SPRUCE, FLORA_TAXON_PINE
    };
    PlaceHomeTree(chunk, treeX, base, treeZ,
                  conifer ? conifers[normalized] : broadleaf[normalized]);
}

int TerrainTestHomeTreeTaxonAt(int treeX, int treeZ)
{
    return (int)HomeTreeTaxonAt(treeX, treeZ, TERRAIN_VARIED, NULL);
}

void TerrainTestPlaceHomeTreeTaxon(Chunk *chunk, int treeX, int base,
                                   int treeZ, int taxonId)
{
    PlaceHomeTree(chunk, treeX, base, treeZ, (FloraTaxonId)taxonId);
}
#endif

static unsigned int PlanetHash2D(int localX, int localZ, unsigned int lane)
{
    int globalX = localX + PlanetWorldOriginX();
    int globalZ = localZ + PlanetWorldOriginZ();
    unsigned int h = Hash2D(globalX + (int)(lane * 101u),
                            globalZ - (int)(lane * 173u));
    h ^= PlanetWorldSeed() + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= h >> 16;
    h *= 2246822519u;
    return h ^ (h >> 13);
}

static float PlanetHashUnit2D(int x, int z, unsigned int lane)
{
    return (float)(PlanetHash2D(x, z, lane) & 0x00ffffffu) / 16777215.0f;
}

static float PlanetNoiseSmooth(float value)
{
    return value * value * (3.0f - 2.0f * value);
}

static float PlanetValueNoise2D(float x, float z, unsigned int lane)
{
    int x0 = (int)floorf(x);
    int z0 = (int)floorf(z);
    float tx = PlanetNoiseSmooth(x - (float)x0);
    float tz = PlanetNoiseSmooth(z - (float)z0);
    float a = Lerp(PlanetHashUnit2D(x0, z0, lane),
                   PlanetHashUnit2D(x0 + 1, z0, lane), tx);
    float b = Lerp(PlanetHashUnit2D(x0, z0 + 1, lane),
                   PlanetHashUnit2D(x0 + 1, z0 + 1, lane), tx);
    return Lerp(a, b, tz);
}

static float PlanetFractalNoise2D(float x, float z, unsigned int lane)
{
    float value = 0.0f;
    float amplitude = 0.58f;
    float total = 0.0f;
    for (int octave = 0; octave < 4; octave++) {
        value += PlanetValueNoise2D(x, z, lane + (unsigned int)octave * 17u) * amplitude;
        total += amplitude;
        x = x * 2.07f + 11.3f;
        z = z * 2.07f - 7.9f;
        amplitude *= 0.48f;
    }
    return value / total;
}

static void PlanetSurfaceCoordinates(int x, int z, float *outX, float *outZ)
{
    *outX = (float)PlanetWorldOriginX() + (float)x;
    *outZ = (float)PlanetWorldOriginZ() + (float)z;
}

static void PlanetMapCoordinatesToLatLon(float mapX, float mapZ,
                                          float *outLongitude, float *outLatitude)
{
    SurfaceMapProjection projection = SurfaceProjectMapCoordinates(
        mapX, mapZ);
    *outLongitude = projection.longitude;
    *outLatitude = projection.latitude;
}

void PlanetSurfaceLatLonAt(int x, int z, float *outLongitude, float *outLatitude)
{
    float mapX = 0.0f;
    float mapZ = 0.0f;
    PlanetSurfaceCoordinates(x, z, &mapX, &mapZ);
    PlanetMapCoordinatesToLatLon(mapX, mapZ, outLongitude, outLatitude);
}

void HomeSurfaceLatLonAt(int x, int z, float *outLongitude, float *outLatitude)
{
    PlanetMapCoordinatesToLatLon((float)x, (float)z,
                                 outLongitude, outLatitude);
}

PlanetSurfaceSample PlanetSurfaceBaselineAt(int x, int z)
{
    float longitude = 0.0f;
    float latitude = 0.0f;
    PlanetSurfaceLatLonAt(x, z, &longitude, &latitude);
    return PlanetSampleGlobalSurfaceBaseline(PlanetWorldSeed(), PlanetWorldProfile(),
                                             longitude, latitude);
}

PlanetSurfaceSample PlanetSurfaceAtTime(int x, int z, double simulationTime)
{
    float longitude = 0.0f;
    float latitude = 0.0f;
    PlanetSurfaceLatLonAt(x, z, &longitude, &latitude);
    return PlanetSampleGlobalSurfaceAtTime(PlanetWorldSeed(), PlanetWorldProfile(),
                                           longitude, latitude, simulationTime);
}

static PlanetSurfaceSample PlanetSampleLocalSurface(int x, int z, float *outX, float *outZ)
{
    float mapX = 0.0f;
    float mapZ = 0.0f;
    PlanetSurfaceCoordinates(x, z, &mapX, &mapZ);
    if (outX) *outX = mapX;
    if (outZ) *outZ = mapZ;
    return PlanetSurfaceBaselineAt(x, z);
}

static int PlanetSeaLevelForProfile(SolarBodyStyle style,
                                    const PlanetProfile *profile)
{
    if (profile->oceanCoverage <= 0.05f) return -1;
    if (style == SOLAR_STYLE_LAVA || style == SOLAR_STYLE_ICE ||
        style == SOLAR_STYLE_TEMPERATE) return 80;
    return -1;
}

int PlanetTerrainSeaLevel(void)
{
    if (!PlanetWorldIsActive()) return TerrainSeaLevel(WorldTerrainMode());
    return PlanetSeaLevelForProfile(PlanetWorldStyle(), PlanetWorldProfile());
}

PlanetBiome PlanetBiomeAt(int x, int z)
{
    if (!PlanetWorldIsActive()) return PLANET_BIOME_PLAINS;
    return PlanetSampleLocalSurface(x, z, NULL, NULL).biome;
}

int PlanetTerrainHeight(int x, int z)
{
    if (!PlanetWorldIsActive()) return TerrainHeight(x, z, WorldTerrainMode());

    const PlanetProfile *profile = PlanetWorldProfile();
    float fx = 0.0f;
    float fz = 0.0f;
    PlanetSurfaceSample surface = PlanetSampleLocalSurface(x, z, &fx, &fz);
    float roughness = Clamp(profile->terrainRoughness, 0.35f, 1.55f);
    float continents = surface.continentalness;
    float localDetail = PlanetFractalNoise2D(fx * 0.024f, fz * 0.024f, 47u);
    float hills = surface.regionalness * 0.62f + surface.detail * 0.23f +
                  localDetail * 0.15f;
    float coast = 0.27f + profile->oceanCoverage * 0.36f;
    int seaLevel = PlanetSeaLevelForProfile(profile->style, profile);
    bool oceanColumn = continents < coast && seaLevel >= 0;
    float height;
    if (oceanColumn) {
        float ocean = Clamp((coast - continents) / fmaxf(coast, 0.08f),
                            0.0f, 1.0f);
        float seamount = SmoothRange(
            0.62f, 0.90f,
            PlanetFractalNoise2D(fx * 0.00105f, fz * 0.00105f, 353u));
        BathymetrySample bathymetry = BathymetryFromSignals(
            seaLevel, ocean, surface.trench, seamount, hills - 0.5f);
        height = (float)bathymetry.seabedY;
    } else {
        float land = Clamp((continents - coast) / fmaxf(1.0f - coast, 0.08f),
                           0.0f, 1.0f);
        float mountainMask = SmoothRange(0.08f, 0.62f, land) *
                             (1.0f - surface.erosion * 0.48f);
        height = 84.0f + land * 30.0f + (hills - 0.5f) * 13.0f;
        height += surface.ridge * mountainMask * 78.0f * roughness;
        height += surface.peak * mountainMask * 44.0f * roughness;
    }

    switch (surface.biome) {
    case PLANET_BIOME_LAVA_SEA:
        height = fminf(height, 74.0f + (hills - 0.5f) * 4.0f);
        break;
    case PLANET_BIOME_VOLCANIC_RIDGE:
        height += 14.0f + surface.volcanicCone * 32.0f;
        break;
    case PLANET_BIOME_BASALT_PLAINS:
        height += sinf(fx * 0.15f) * cosf(fz * 0.13f) * 2.5f;
        break;
    case PLANET_BIOME_GLACIER:
        height = fminf(height, 76.0f + (hills - 0.5f) * 7.0f);
        {
            float latitude = SurfaceProjectMapCoordinates(fx, fz).latitude;
            // Ice flows downhill from each pole toward the equator. The
            // latitude gradient gives the cut plane a consistent flow axis.
            height += surface.glacierFlow * (fabsf(latitude) - 0.70f) * 18.0f;
        }
        height -= surface.glacierCracks * 1.4f;
        break;
    case PLANET_BIOME_ICE_SHEET:
        height += 8.0f + sinf((fx + fz) * 0.008f) * 6.0f;
        break;
    case PLANET_BIOME_DUNES:
        {
            float wind = profile->prevailingWindAngle;
            SurfaceMapProjection projection = SurfaceProjectMapCoordinates(
                fx, fz);
            float longitude = projection.longitude;
            float latitude = projection.latitude;
            float latitudeCos = cosf(latitude);
            Vector3 point = { latitudeCos * cosf(longitude), sinf(latitude),
                              latitudeCos * sinf(longitude) };
            Vector3 windAxis = { cosf(wind), 0.0f, sinf(wind) };
            Vector3 crossAxis = { -sinf(wind), 0.0f, cosf(wind) };
            float windCoord = Vector3DotProduct(point, windAxis);
            float crossWind = Vector3DotProduct(point, crossAxis);
            float duneShape = 0.5f + 0.5f * sinf(windCoord * 24.0f +
                                                    crossWind * 5.0f);
            height += duneShape * surface.duneBand * 12.0f;
        }
        break;
    case PLANET_BIOME_BADLANDS:
        height += 8.0f + fabsf(sinf(fx * 0.026f) * cosf(fz * 0.022f)) * 16.0f;
        break;
    case PLANET_BIOME_OASIS:
        height = fminf(height, 84.0f + (hills - 0.5f) * 4.0f);
        break;
    case PLANET_BIOME_MAGMA_MIRE:
        height = fminf(height, 84.0f + (hills - 0.5f) * 3.0f);
        break;
    case PLANET_BIOME_FROZEN_MIRE:
        height = fminf(height, 86.0f + (hills - 0.5f) * 3.0f);
        break;
    case PLANET_BIOME_SALT_MARSH:
        height = fminf(height, 84.0f + (hills - 0.5f) * 3.0f);
        break;
    case PLANET_BIOME_CRATER_BOG:
        height = fminf(height, 82.0f + (hills - 0.5f) * 3.0f);
        break;
    case PLANET_BIOME_TEMPERATE_MARSH:
        height = fminf(height, 86.0f + (hills - 0.5f) * 3.0f);
        break;
    case PLANET_BIOME_IMPACT_BASIN:
        height -= 14.0f + (1.0f - hills) * 10.0f;
        break;
    case PLANET_BIOME_CRATER_HIGHLANDS:
        height += fabsf(sinf(fx * 0.021f) * cosf(fz * 0.019f)) * 10.0f;
        break;
    case PLANET_BIOME_OCEAN:
        if (!oceanColumn) {
            height = fminf(height, 76.0f + (hills - 0.5f) * 5.0f);
        }
        break;
    case PLANET_BIOME_COAST:
        height = fminf(height, 87.0f + (hills - 0.5f) * 6.0f);
        break;
    case PLANET_BIOME_ALPINE:
        height += 18.0f + (hills - 0.35f) * 20.0f * roughness;
        break;
    case PLANET_BIOME_STORM_BANDS:
        height = 112.0f + sinf(fz * 0.025f) * 14.0f +
                 sinf(fx * 0.009f) * 10.0f;
        break;
    case PLANET_BIOME_PLAINS:
    case PLANET_BIOME_FOREST:
    default:
        height += sinf(fx * 0.024f) * cosf(fz * 0.021f) * 5.0f;
        break;
    }

    switch (profile->canonicalBodyId) {
    case 1u:
        height += (surface.detail - 0.5f) * 12.0f;
        break;
    case 2u:
        height = 82.0f + (height - 82.0f) * 0.62f;
        height += surface.ridge * 9.0f + surface.volcanicCone * 18.0f;
        break;
    case 4u:
        height -= surface.trench * 24.0f;
        height += surface.peak * 16.0f + surface.volcanicCone * 24.0f;
        break;
    default:
        break;
    }

    // These fields are shared with the orbital map: the same crater walls,
    // ejecta blankets, volcanic cones and lava channels continue at landing.
    height -= surface.impactDepth * 26.0f;
    height += surface.impactRim * 18.0f + surface.ejecta * 7.0f;
    height += surface.volcanicCone * 30.0f;
    height -= surface.caldera * 18.0f;
    height += surface.lavaFlow * (profile->style == SOLAR_STYLE_LAVA ? 7.0f : 2.5f);

    height = roundf(height);
    if (oceanColumn) {
        height = Clamp(height, (float)BATHYMETRY_MIN_SEABED_Y,
                       (float)(seaLevel - 1));
    } else if (height < 5.0f) {
        height = 5.0f;
    }
    if (height > 240.0f) height = 240.0f;
    return (int)height;
}

BathymetrySample PlanetBathymetryAt(int x, int z)
{
    if (!PlanetWorldIsActive()) return TerrainBathymetryAt(x, z, WorldTerrainMode());

    int seaLevel = PlanetTerrainSeaLevel();
    int height = PlanetTerrainHeight(x, z);
    if (seaLevel < 0 || height >= seaLevel) {
        return (BathymetrySample){
            .seaLevel = seaLevel,
            .seabedY = height,
            .waterDepth = 0,
            .zone = BATHYMETRY_ZONE_LAND,
            .material = BATHYMETRY_MATERIAL_ROCK
        };
    }

    float fx = 0.0f;
    float fz = 0.0f;
    PlanetSurfaceSample surface = PlanetSampleLocalSurface(x, z, &fx, &fz);
    float seamount = SmoothRange(
        0.62f, 0.90f,
        PlanetFractalNoise2D(fx * 0.00105f, fz * 0.00105f, 353u));
    BathymetrySample sample = BathymetryForHeight(
        seaLevel, height, surface.trench, seamount);
    return sample;
}

static bool SurfaceLandingCandidate(int x, int z, int footprintRadius,
                                    int *outGroundY)
{
    bool planetSurface = PlanetWorldIsActive();
    int seaLevel = planetSurface ? PlanetTerrainSeaLevel()
                                 : TerrainSeaLevel(WorldTerrainMode());
    int surfaceMinY = planetSurface ? SURFACE_GENERATION_MIN_Y
                                    : SURFACE_MIN_Y;
    int surfaceMaxYExclusive = planetSurface
        ? SURFACE_GENERATION_MAX_Y_EXCLUSIVE
        : SURFACE_MAX_Y_EXCLUSIVE;
    int minHeight = 0;
    int maxHeight = 0;
    bool haveHeight = false;
    for (int dz = -footprintRadius; dz <= footprintRadius; dz++) {
        for (int dx = -footprintRadius; dx <= footprintRadius; dx++) {
            int height = planetSurface
                             ? PlanetTerrainHeight(x + dx, z + dz)
                             : TerrainHeight(x + dx, z + dz, WorldTerrainMode());
            if (seaLevel >= 0 && height <= seaLevel + 1) return false;
            if (!haveHeight) {
                minHeight = height;
                maxHeight = height;
                haveHeight = true;
            } else {
                if (height < minHeight) minHeight = height;
                if (height > maxHeight) maxHeight = height;
            }
        }
    }
    if (!haveHeight || minHeight < surfaceMinY ||
        maxHeight - minHeight > 1 ||
        maxHeight > surfaceMaxYExclusive - 6) return false;
    if (outGroundY) *outGroundY = maxHeight;
    return true;
}

bool FindSafeSurfaceLanding(int preferredX, int preferredZ, int maxRadius,
                            int footprintRadius, int *outX, int *outZ,
                            int *outGroundY)
{
    if (maxRadius < 0 || footprintRadius < 0) return false;
    for (int radius = 0; radius <= maxRadius; radius++) {
        int step = radius > 24 ? 2 : 1;
        if (radius == 0) {
            int ground = 0;
            if (SurfaceLandingCandidate(preferredX, preferredZ,
                                        footprintRadius, &ground)) {
                if (outX) *outX = preferredX;
                if (outZ) *outZ = preferredZ;
                if (outGroundY) *outGroundY = ground;
                return true;
            }
            continue;
        }
        for (int offset = -radius; offset <= radius; offset += step) {
            const int candidates[4][2] = {
                { offset, -radius }, { radius, offset },
                { -offset, radius }, { -radius, -offset }
            };
            for (int i = 0; i < 4; i++) {
                int x = preferredX + candidates[i][0];
                int z = preferredZ + candidates[i][1];
                int ground = 0;
                if (!SurfaceLandingCandidate(x, z, footprintRadius,
                                             &ground)) continue;
                if (outX) *outX = x;
                if (outZ) *outZ = z;
                if (outGroundY) *outGroundY = ground;
                return true;
            }
        }
    }
    return false;
}

#ifdef TERRAIN_TESTING
BlockType TerrainTestPlanetSubsurfaceBlock(SolarBodyStyle style,
                                           PlanetBiome biome, int depth,
                                           unsigned int hash)
{
    return TerrainGeologyPlanetSubsurfaceBlock(style, biome, depth, hash);
}
#endif

static SubsurfaceParams PlanetSubsurfaceParamsFor(
    SolarBodyStyle style, const PlanetProfile *profile)
{
    float roughness = profile ? profile->terrainRoughness : 1.0f;
    float oceanCoverage = profile ? profile->oceanCoverage : 0.0f;
    SubsurfaceParams params = {
        .seed = PlanetWorldSeed(),
        .activity = Clamp(0.72f + roughness * 0.38f, 0.55f, 1.35f),
        .minY = 2,
        .surfaceClearance = 4,
        .aquiferLevel = 36,
        .aquiferChance = Clamp(oceanCoverage * 0.95f, 0.0f, 0.88f)
    };
    switch (style) {
    case SOLAR_STYLE_LAVA:
        params.activity = fmaxf(params.activity, 1.18f);
        params.aquiferLevel = 42;
        params.aquiferChance = 0.72f;
        break;
    case SOLAR_STYLE_ICE:
        params.activity *= 0.88f;
        params.aquiferLevel = 32;
        params.aquiferChance = 0.54f;
        break;
    case SOLAR_STYLE_DESERT:
        params.activity *= 0.94f;
        params.aquiferChance *= 0.22f;
        break;
    case SOLAR_STYLE_GAS:
        params.activity = 0.78f;
        params.aquiferChance = 0.08f;
        break;
    case SOLAR_STYLE_CRATER:
        params.activity = fmaxf(params.activity, 1.05f);
        params.aquiferChance *= 0.12f;
        break;
    case SOLAR_STYLE_TEMPERATE:
    default:
        params.aquiferChance = Clamp(0.22f + oceanCoverage * 0.78f,
                                     0.0f, 0.90f);
        break;
    }
    return params;
}

TerrainSubsurfaceLiquidSummary TerrainSubsurfaceLiquidSummaryAt(
    int x, int z, int surfaceHeight)
{
    SubsurfaceParams params = HomeSubsurfaceParams();
    TerrainSubsurfaceLiquidKind kind = TERRAIN_SUBSURFACE_LIQUID_WATER;
    if (PlanetWorldIsActive()) {
        SolarBodyStyle style = PlanetWorldStyle();
        params = PlanetSubsurfaceParamsFor(style, PlanetWorldProfile());
        if (style == SOLAR_STYLE_LAVA) {
            kind = TERRAIN_SUBSURFACE_LIQUID_LAVA;
        }
    }

    TerrainSubsurfaceLiquidSummary summary = { 0 };
    int maxY = surfaceHeight - params.surfaceClearance - 1;
    int aquiferCeiling = params.aquiferLevel + 12;
    if (maxY > aquiferCeiling) maxY = aquiferCeiling;
    if (maxY < params.minY) return summary;

    int floodedCount = 0;
    int sampleCount = maxY - params.minY + 1;
    for (int y = params.minY; y <= maxY; y++) {
        SubsurfaceSample sample = SubsurfaceSampleAt(
            &params, x, y, z, surfaceHeight);
        if (!sample.flooded) continue;
        if (floodedCount == 0) summary.minY = y;
        summary.maxY = y;
        floodedCount++;
    }
    if (floodedCount == 0) return summary;
    summary.kind = kind;
    summary.floodedFraction = (float)floodedCount / (float)sampleCount;
    return summary;
}

static bool PlanetWetlandBiome(PlanetBiome biome)
{
    return biome == PLANET_BIOME_TEMPERATE_MARSH ||
           biome == PLANET_BIOME_SALT_MARSH ||
           biome == PLANET_BIOME_FROZEN_MIRE ||
           biome == PLANET_BIOME_MAGMA_MIRE ||
           biome == PLANET_BIOME_CRATER_BOG;
}

static void GeneratePlanetChunkTerrain(Chunk *chunk, int cx, int cz)
{
    ChunkClearBlockStorage(chunk);
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    SolarBodyStyle style = PlanetWorldStyle();
    const PlanetProfile *profile = PlanetWorldProfile();
    SubsurfaceParams subsurface = PlanetSubsurfaceParamsFor(style, profile);
    int seaLevel = PlanetSeaLevelForProfile(style, profile);

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int worldX = startX + lx;
            int worldZ = startZ + lz;
            int localX = worldX;
            int localZ = worldZ;
            BathymetrySample bathymetry = PlanetBathymetryAt(worldX, worldZ);
            int height = bathymetry.seabedY;
            PlanetSurfaceSample surface = PlanetSampleLocalSurface(worldX, worldZ, NULL, NULL);
            PlanetBiome biome = surface.biome;

            for (int y = 0; y <= height; y++) {
                unsigned int h = PlanetHash2D(
                    FloorDivInt(localX, 3) + FloorDivInt(y, 2) * 19,
                    FloorDivInt(localZ, 3) - FloorDivInt(y, 2) * 23, 1u);
                int depth = height - y;
                BlockType type = y == 0 ? BLOCK_BEDROCK :
                                 TerrainGeologyPlanetSubsurfaceBlock(
                                     style, biome, depth, h);
                if (seaLevel >= 0 && height < seaLevel && y == height &&
                    style != SOLAR_STYLE_LAVA && style != SOLAR_STYLE_ICE) {
                    type = BathymetryMaterialBlock(bathymetry.material);
                    if ((biome == PLANET_BIOME_COAST ||
                         biome == PLANET_BIOME_OCEAN) &&
                        bathymetry.material == BATHYMETRY_MATERIAL_SAND &&
                        PlanetHash2D(FloorDivInt(worldX, 6),
                                     FloorDivInt(worldZ, 6), 503u) % 5u == 0u) {
                        type = BLOCK_CLAY;
                    }
                }
                SubsurfaceSample cave = SubsurfaceSampleAt(
                    &subsurface, worldX, y, worldZ, height);
                if (cave.cave) {
                    type = cave.flooded && style == SOLAR_STYLE_LAVA
                               ? BLOCK_LAVA
                               : (cave.flooded ? BLOCK_WATER : BLOCK_AIR);
                }
                ChunkSetLocalBlock(chunk, lx, y, lz, type);
            }

            if (height > 1 && PlanetWetlandBiome(biome) &&
                PlanetHash2D(worldX, worldZ, 557u) % 11u == 0u) {
                BlockType liquid = biome == PLANET_BIOME_MAGMA_MIRE
                    ? BLOCK_LAVA
                    : (biome == PLANET_BIOME_FROZEN_MIRE
                           ? BLOCK_ICE : BLOCK_WATER);
                ChunkSetLocalBlock(chunk, lx, height, lz, liquid);
            }

            if (seaLevel >= 0 && height < seaLevel) {
                BlockType liquid = style == SOLAR_STYLE_LAVA ? BLOCK_LAVA : BLOCK_WATER;
                if (surface.iceCoverage > 0.64f) liquid = BLOCK_ICE;
                for (int y = height + 1; y <= seaLevel && InHeight(y); y++) {
                    ChunkSetLocalBlock(chunk, lx, y, lz, liquid);
                }
                if (style == SOLAR_STYLE_ICE && InHeight(seaLevel)) {
                    ChunkSetLocalBlock(chunk, lx, seaLevel, lz, BLOCK_ICE);
                }
            }

            unsigned int decor = PlanetHash2D(localX, localZ, 7u);
            if (!InHeight(height + 1)) continue;
            if (surface.glacierCracks > 0.84f && decor % 67u == 0u) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_MOON_ROCK);
            } else if (surface.lavaFlow > 0.82f && decor % 53u == 0u) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_GLOWSTONE);
            } else if (surface.ejecta > 0.76f && decor % 83u == 0u) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_METEORITE);
            } else if ((biome == PLANET_BIOME_DUNES || biome == PLANET_BIOME_BADLANDS) &&
                decor % (biome == PLANET_BIOME_DUNES ? 181u : 293u) == 0u &&
                ((float)(PlanetHash2D(worldX, worldZ, 401u) & 0x00ffffffu) /
                 16777215.0f) <= PlanetEcologyStaticSuitabilityAt(
                     worldX, worldZ).floraCapacity) {
                for (int y = height + 1; y <= height + 3 && InHeight(y); y++) {
                    ChunkSetLocalBlock(chunk, lx, y, lz, BLOCK_CACTUS);
                }
            } else if (biome == PLANET_BIOME_GLACIER && decor % 137u == 0u) {
                for (int y = height + 1; y <= height + 4 && InHeight(y); y++) {
                    ChunkSetLocalBlock(chunk, lx, y, lz, BLOCK_ICE);
                }
            } else if (biome == PLANET_BIOME_ICE_SHEET && decor % 211u == 0u) {
                for (int y = height + 1; y <= height + 2 && InHeight(y); y++) {
                    ChunkSetLocalBlock(chunk, lx, y, lz, BLOCK_ICE);
                }
            } else if ((biome == PLANET_BIOME_BASALT_PLAINS ||
                        biome == PLANET_BIOME_VOLCANIC_RIDGE) && decor % 193u == 0u) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_GLOWSTONE);
            } else if (biome == PLANET_BIOME_STORM_BANDS &&
                       decor % 113u == 0u) {
                int crystalHeight = 2 + (int)(decor % 3u);
                for (int y = height + 1;
                     y <= height + crystalHeight && InHeight(y); y++) {
                    ChunkSetLocalBlock(chunk, lx, y, lz, BLOCK_CRYSTAL);
                }
            } else if (biome == PLANET_BIOME_STORM_BANDS && decor % 157u == 0u &&
                       ((float)(PlanetHash2D(worldX, worldZ, 409u) & 0x00ffffffu) /
                        16777215.0f) <= PlanetEcologyStaticSuitabilityAt(
                            worldX, worldZ).floraCapacity) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_MUSHROOM);
            } else if ((biome == PLANET_BIOME_IMPACT_BASIN ||
                        biome == PLANET_BIOME_CRATER_HIGHLANDS) && decor % 149u == 0u) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_METEORITE);
            } else if ((biome == PLANET_BIOME_FOREST || biome == PLANET_BIOME_PLAINS ||
                        biome == PLANET_BIOME_OASIS) && height > seaLevel + 1 &&
                       biome == PLANET_BIOME_FOREST && decor % 3u == 0u &&
                       ((float)(PlanetHash2D(worldX, worldZ, 417u) & 0x00ffffffu) /
                        16777215.0f) <= PlanetEcologyStaticSuitabilityAt(
                            worldX, worldZ).floraCapacity) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz,
                                   BLOCK_LEAF_LITTER);
            } else if ((biome == PLANET_BIOME_FOREST || biome == PLANET_BIOME_PLAINS ||
                        biome == PLANET_BIOME_OASIS) && height > seaLevel + 1 &&
                       decor % (biome == PLANET_BIOME_FOREST ? 61u : 97u) == 0u &&
                       ((float)(PlanetHash2D(worldX, worldZ, 419u) & 0x00ffffffu) /
                        16777215.0f) <= PlanetEcologyStaticSuitabilityAt(
                            worldX, worldZ).floraCapacity) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_FLOWER);
            }
        }
    }

    PlanetEcologyApplyToChunk(chunk, cx, cz);
    if (planetChunkDecorator) planetChunkDecorator(chunk, cx, cz);
}

BlockType TerrainBaseBlockAt(int x, int y, int z, TerrainMode mode)
{
    if (!InHeight(y) || y >= SURFACE_GENERATION_MAX_Y_EXCLUSIVE ||
        (mode == TERRAIN_FLAT && y < SURFACE_GENERATION_MIN_Y)) {
        return BLOCK_AIR;
    }
    SurfaceTerrainSample surface = SurfaceTerrainAt(x, z, mode);
    return TerrainHomeBaseBlockFromSample(
        x, y, z, mode, &surface, TerrainSeaLevel(mode));
}

static void SampleHomeChunkColumns(
    int cx, int cz, TerrainMode mode,
    SurfaceTerrainSample samples[CHUNK_SIZE][CHUNK_SIZE])
{
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            samples[lx][lz] = SurfaceTerrainAt(
                startX + lx, startZ + lz, mode);
        }
    }
}

static bool TerrainSectionFaceIsVisible(BlockType current,
                                        BlockType neighbor)
{
    if (neighbor == BLOCK_AIR || neighbor == BLOCK_SPACESHIP_OCCUPIED) {
        return true;
    }
    if (!IsTranslucentBlock(current)) return IsTranslucentBlock(neighbor);
    return IsTranslucentBlock(neighbor) && neighbor != current;
}

bool TerrainSectionHasExposedFaces(const ChunkSection *section, int cx,
                                   int cz, int sectionY, TerrainMode mode)
{
    if (!section || section->sectionY != sectionY) return false;
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    int startX = cx * CHUNK_SIZE;
    int startY = sectionY * SURFACE_SECTION_HEIGHT;
    int startZ = cz * CHUNK_SIZE;

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int ly = 0; ly < SURFACE_SECTION_HEIGHT; ly++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                BlockType current = (BlockType)section->blocks[lx][ly][lz];
                if (current == BLOCK_AIR) continue;
                for (int face = 0; face < 6; face++) {
                    int neighborX = lx + faces[face][0];
                    int neighborY = ly + faces[face][1];
                    int neighborZ = lz + faces[face][2];
                    BlockType neighbor = BLOCK_AIR;
                    if (neighborX >= 0 && neighborX < CHUNK_SIZE &&
                        neighborY >= 0 &&
                        neighborY < SURFACE_SECTION_HEIGHT &&
                        neighborZ >= 0 && neighborZ < CHUNK_SIZE) {
                        neighbor = (BlockType)section->blocks
                            [neighborX][neighborY][neighborZ];
                    } else {
                        neighbor = TerrainBaseBlockAt(
                            startX + neighborX, startY + neighborY,
                            startZ + neighborZ, mode);
                    }
                    if (TerrainSectionFaceIsVisible(current, neighbor)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

static bool GenerateChunkTerrainSectionBaseFromSamples(
    Chunk *chunk, int cx, int cz, int sectionY, TerrainMode mode,
    const SurfaceTerrainSample samples[CHUNK_SIZE][CHUNK_SIZE])
{
    if (!chunk || !samples || !SurfaceSectionInBounds(sectionY) ||
        ChunkTerrainSectionIsResolved(chunk, sectionY) ||
        ChunkGetSectionConst(chunk, sectionY)) {
        return false;
    }

    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int firstY = sectionY * SURFACE_SECTION_HEIGHT;
    int lastY = firstY + SURFACE_SECTION_HEIGHT;
    int seaLevel = TerrainSeaLevel(mode);
    chunk->cx = cx;
    chunk->cz = cz;

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int worldX = startX + lx;
            int worldZ = startZ + lz;
            for (int y = firstY; y < lastY; y++) {
                BlockType type = TerrainHomeBaseBlockFromSample(
                    worldX, y, worldZ, mode, &samples[lx][lz], seaLevel);
                if (type != BLOCK_AIR &&
                    !ChunkSetLocalBlock(chunk, lx, y, lz, type)) {
                    return false;
                }
            }
        }
    }
    return ChunkMarkTerrainSectionResolved(chunk, sectionY);
}

bool GenerateChunkTerrainSectionBase(Chunk *chunk, int cx, int cz,
                                     int sectionY, TerrainMode mode)
{
    if (!chunk || !SurfaceSectionInBounds(sectionY) ||
        ChunkTerrainSectionIsResolved(chunk, sectionY) ||
        ChunkGetSectionConst(chunk, sectionY)) {
        return false;
    }
    SurfaceTerrainSample samples[CHUNK_SIZE][CHUNK_SIZE];
    SampleHomeChunkColumns(cx, cz, mode, samples);
    return GenerateChunkTerrainSectionBaseFromSamples(
        chunk, cx, cz, sectionY, mode, samples);
}

static void GenerateHomeVisibleTerrainSections(
    Chunk *chunk, int cx, int cz, TerrainMode mode,
    const SurfaceTerrainSample samples[CHUNK_SIZE][CHUNK_SIZE])
{
    int seaLevel = TerrainSeaLevel(mode);
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int height = (int)lroundf(samples[lx][lz].elevation);
            int top = seaLevel >= 0 && height < seaLevel ? seaLevel : height;
            int topSectionY = SurfaceSectionYFromBlockY(top);
            GenerateChunkTerrainSectionBaseFromSamples(
                chunk, cx, cz, topSectionY, mode, samples);
            if (SurfaceSectionInBounds(topSectionY - 1)) {
                GenerateChunkTerrainSectionBaseFromSamples(
                    chunk, cx, cz, topSectionY - 1, mode, samples);
            }
        }
    }
}

static void ResolveMaterializedTerrainSections(Chunk *chunk)
{
    if (!chunk) return;
    for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
         sectionIndex++) {
        ChunkMarkTerrainSectionResolved(
            chunk, chunk->sections[sectionIndex]->sectionY);
    }
}

#ifdef TERRAIN_TESTING
static BlockType HomeGroundCoverBlock(Biome biome, int height, int seaLevel,
                                      unsigned int hash)
{
    if (hash % 173u == 0u) return BLOCK_FLOWER;
    if (hash % 397u == 0u) return BLOCK_MUSHROOM;
    if (biome == BIOME_FOREST && hash % 3u == 0u) {
        return BLOCK_LEAF_LITTER;
    }
    if (biome == BIOME_FOREST && hash % 13u == 0u) return BLOCK_FERN;
    if (biome == BIOME_FOREST && hash % 19u == 0u) return BLOCK_MOSS_CARPET;
    if (biome == BIOME_SWAMP && hash % 3u == 0u) return BLOCK_REED;
    if (biome == BIOME_SWAMP && hash % 5u == 0u) return BLOCK_FERN;
    if (biome == BIOME_SWAMP && hash % 11u == 0u) return BLOCK_MOSS_CARPET;
    if ((biome == BIOME_PLAINS || biome == BIOME_FOREST) &&
        height <= seaLevel + 4 && hash % 23u == 0u) return BLOCK_REED;
    if ((biome == BIOME_PLAINS || biome == BIOME_FOREST) &&
        hash % 7u == 0u) return BLOCK_TALL_GRASS;
    return BLOCK_AIR;
}
#endif

static bool HomeFloraSubstrate(BlockType block)
{
    switch (block) {
    case BLOCK_GRASS:
    case BLOCK_DIRT:
    case BLOCK_SAND:
    case BLOCK_RED_SAND:
    case BLOCK_MUD:
    case BLOCK_LOAM:
    case BLOCK_PODZOL:
    case BLOCK_PEAT:
    case BLOCK_CHERNOZEM:
    case BLOCK_TERRA_ROSSA:
    case BLOCK_ALLUVIUM:
    case BLOCK_HUMUS:
    case BLOCK_COMPOST:
    case BLOCK_FIRE_ASH:
    case BLOCK_CHARCOAL:
        return true;
    default:
        return false;
    }
}

static BlockType HomeTaxonGroundCoverBlock(
    int x, int z, TerrainMode mode, const SurfaceTerrainSample *surface,
    BlockType substrate, unsigned int hash)
{
    if (surface->biome == BIOME_DESERT) return BLOCK_AIR;
    unsigned divisor = 9u;
    switch (surface->biome) {
    case BIOME_FOREST: divisor = 5u; break;
    case BIOME_PLAINS: divisor = 6u; break;
    case BIOME_SWAMP: divisor = 3u; break;
    case BIOME_MOUNTAIN: divisor = 11u; break;
    case BIOME_SNOW: divisor = 17u; break;
    default: break;
    }
    if (hash % divisor != 0u) {
        if (surface->biome == BIOME_FOREST && hash % 13u == 0u) {
            return BLOCK_LEAF_LITTER;
        }
        return BLOCK_AIR;
    }
    FloraHabitat habitat = HomeFloraHabitatAt(x, z, mode, surface);
    habitat.substrate = substrate;
    FloraTaxonId selected = FloraSelectTaxon(
        &habitat, WorldHash2DBits(hash ^ 0x4d595df4u,
                                 hash ^ 0x8f3f73b5u), false);
    const FloraTaxon *taxon = FloraTaxonAt(selected);
    return taxon ? taxon->primaryBlock : BLOCK_AIR;
}

FloraTaxonId TerrainHomeGroundTaxonAt(int x, int z, TerrainMode mode,
                                      BlockType substrate, uint32_t hash)
{
    SurfaceTerrainSample surface = SurfaceTerrainAt(x, z, mode);
    FloraHabitat habitat = HomeFloraHabitatAt(x, z, mode, &surface);
    habitat.substrate = substrate;
    return FloraSelectTaxon(
        &habitat, WorldHash2DBits(hash ^ 0x4d595df4u,
                                 hash ^ 0x8f3f73b5u), false);
}

static void PlaceSaguaro(Chunk *chunk, int x, int base, int z,
                         unsigned int hash)
{
    int height = 4 + (int)(hash % 4u);
    for (int y = base; y < base + height && InHeight(y); y++) {
        SetChunkLocalBlock(chunk, x, y, z, BLOCK_SAGUARO);
    }
    int firstDirection = (int)((hash >> 5) & 3u);
    int armCount = 1 + (int)((hash >> 8) % 2u);
    for (int arm = 0; arm < armCount; arm++) {
        int directionX = 0;
        int directionZ = 0;
        HomeTreeDirection(firstDirection + arm * 2, &directionX, &directionZ);
        int jointY = base + 2 + (int)((hash >> (10 + arm * 3)) %
                                     (unsigned)(height - 2));
        SetChunkLocalBlock(chunk, x + directionX, jointY,
                           z + directionZ, BLOCK_SAGUARO);
        SetChunkLocalBlock(chunk, x + directionX, jointY + 1,
                           z + directionZ, BLOCK_SAGUARO);
    }
}

void GenerateChunkTerrain(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    chunk->cx = cx;
    chunk->cz = cz;
    chunk->floraStructureCount = 0;
    if (PlanetWorldIsActive()) {
        GeneratePlanetChunkTerrain(chunk, cx, cz);
        return;
    }

    ChunkClearBlockStorage(chunk);

    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    SurfaceTerrainSample samples[CHUNK_SIZE][CHUNK_SIZE];
    SampleHomeChunkColumns(cx, cz, mode, samples);

    int seaLevel = TerrainSeaLevel(mode);
    GenerateHomeVisibleTerrainSections(chunk, cx, cz, mode, samples);

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int worldX = startX + lx;
            int worldZ = startZ + lz;
            const SurfaceTerrainSample *surface = &samples[lx][lz];
            int height = (int)lroundf(surface->elevation);
            Biome biome = surface->biome;
            bool submerged = seaLevel >= 0 && height < seaLevel;

            if (mode != TERRAIN_FLAT && !submerged &&
                biome == BIOME_DESERT && height > 6 &&
                (WorldHash2D(worldX, worldZ) % 29u) == 0u) {
                PlaceSaguaro(chunk, worldX, height + 1, worldZ,
                             WorldHash2D(worldX, worldZ + 13));
            }
        }
    }

    if (mode != TERRAIN_FLAT) {
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int worldX = startX + lx;
                int worldZ = startZ + lz;
                const SurfaceTerrainSample *sample = &samples[lx][lz];
                int height = (int)lroundf(sample->elevation);
                if (height < 4) continue;
                BlockType ground = ChunkGetLocalBlock(chunk, lx, height, lz);
                if (!HomeFloraSubstrate(ground)) continue;
                unsigned int h = WorldHash2D(worldX, worldZ);
                BlockType cover = HomeTaxonGroundCoverBlock(
                    worldX, worldZ, mode, sample, ground, h);
                if (cover != BLOCK_AIR) {
                    SetChunkLocalBlock(chunk, worldX, height + 1, worldZ,
                                       cover);
                }
            }
        }
    }

    for (int treeX = startX - HOME_TREE_MAX_CROWN_RADIUS;
         treeX < startX + CHUNK_SIZE + HOME_TREE_MAX_CROWN_RADIUS;
         treeX++) {
        for (int treeZ = startZ - HOME_TREE_MAX_CROWN_RADIUS;
             treeZ < startZ + CHUNK_SIZE + HOME_TREE_MAX_CROWN_RADIUS;
             treeZ++) {
            if (!ShouldPlaceTree(treeX, treeZ, mode)) continue;

            int base = TerrainHeight(treeX, treeZ, mode) + 1;
            FloraTaxonId taxon = HomeTreeTaxonAt(
                treeX, treeZ, mode, NULL);
            PlaceHomeTree(chunk, treeX, base, treeZ, taxon);
        }
    }

    if (mode != TERRAIN_FLAT) {
        TerrainStructuresGenerate(chunk, cx, cz, mode);
    }

    // Full-column generation has evaluated the procedural baseline for every
    // section materialized by surface decorations or structures.
    ResolveMaterializedTerrainSections(chunk);
}

#ifdef TERRAIN_TESTING
BlockType TerrainTestHomeGroundCoverBlock(
    Biome biome, int height, int seaLevel, unsigned int hash)
{
    return HomeGroundCoverBlock(biome, height, seaLevel, hash);
}

void TerrainTestBootstrapHomeChunk(Chunk *chunk, int cx, int cz,
                                   TerrainMode mode)
{
    if (!chunk) return;
    chunk->cx = cx;
    chunk->cz = cz;
    ChunkClearBlockStorage(chunk);
    SurfaceTerrainSample samples[CHUNK_SIZE][CHUNK_SIZE];
    SampleHomeChunkColumns(cx, cz, mode, samples);
    GenerateHomeVisibleTerrainSections(chunk, cx, cz, mode, samples);
}

void TerrainTestResolveMaterializedSections(Chunk *chunk)
{
    ResolveMaterializedTerrainSections(chunk);
}

#endif

void ApplyEditsToChunkSection(Chunk *chunk, int sectionY)
{
    if (!chunk) return;
    int editCount = WorldGetEditCount();
    for (int i = 0; i < editCount; i++) {
        BlockEdit edit;
        if (!WorldGetEditForCurrentDimension(i, &edit)) continue;
        int editCx = 0;
        int editCz = 0;
        int editLx = 0;
        int editLz = 0;
        WorldToChunkLocal(edit.x, edit.z, &editCx, &editCz, &editLx, &editLz);
        if (editCx == chunk->cx && editCz == chunk->cz && InHeight(edit.y) &&
            SurfaceSectionYFromBlockY(edit.y) == sectionY) {
            ChunkSetLocalBlock(chunk, editLx, edit.y, editLz, edit.type);
        }
    }
}

void ApplyEditsToChunk(Chunk *chunk)
{
    if (!chunk) return;
    int editCount = WorldGetEditCount();
    for (int i = 0; i < editCount; i++) {
        BlockEdit edit;
        if (!WorldGetEditForCurrentDimension(i, &edit)) continue;
        int editCx = 0;
        int editCz = 0;
        int editLx = 0;
        int editLz = 0;
        WorldToChunkLocal(edit.x, edit.z, &editCx, &editCz, &editLx, &editLz);
        if (editCx == chunk->cx && editCz == chunk->cz && InHeight(edit.y)) {
            int sectionY = SurfaceSectionYFromBlockY(edit.y);
            if (!ChunkTerrainSectionIsResolved(chunk, sectionY) &&
                !ChunkGetSectionConst(chunk, sectionY) &&
                HomeWorldSurfaceIsActive() &&
                !GenerateChunkTerrainSectionBase(
                    chunk, chunk->cx, chunk->cz, sectionY,
                    WorldTerrainMode())) {
                continue;
            }
            ChunkSetLocalBlock(chunk, editLx, edit.y, editLz, edit.type);
        }
    }
}
