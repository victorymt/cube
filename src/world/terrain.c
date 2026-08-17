#include "world/terrain.h"

#include "world/subsurface.h"
#include "world/surface_topology.h"
#include "world/terrain_geology_internal.h"

#include "gameplay/discovery.h"
#include "ecology/ecology.h"

#include "raymath.h"
#include "world/chunks.h"
#include "space/space.h"
#include "world/world.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "world/chunks.h"
#include "world/world.h"
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
    if (sample.elevation >= 132.0f || sample.ridge > 0.58f) {
        sample.biome = BIOME_MOUNTAIN;
    } else if (climate < 0.25f) {
        sample.biome = BIOME_SNOW;
    } else if (climate < 0.43f) {
        sample.biome = BIOME_DESERT;
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

typedef enum HomeTreeKind {
    HOME_TREE_BROADLEAF_ROUND = 0,
    HOME_TREE_BROADLEAF_SPREADING,
    HOME_TREE_BROADLEAF_COLUMNAR,
    HOME_TREE_CONIFER_SPRUCE,
    HOME_TREE_CONIFER_PINE
} HomeTreeKind;

static int HomeTreeDensityDivisor(Biome biome)
{
    switch (biome) {
    case BIOME_FOREST:   return 47;
    case BIOME_PLAINS:   return 131;
    case BIOME_MOUNTAIN: return 149;
    case BIOME_SNOW:     return 67;
    default:             return 0;
    }
}

static unsigned int HomeTreeShapeHash(int treeX, int treeZ)
{
    return WorldHash2DBits((unsigned int)treeX ^ 0xa511e9b3u,
                           (unsigned int)treeZ ^ 0x63d83595u);
}

static HomeTreeKind HomeTreeKindForFamily(int treeX, int treeZ,
                                          bool conifer)
{
    unsigned int roll = HomeTreeShapeHash(treeX, treeZ) % 100u;
    if (conifer) {
        return roll < 62u ? HOME_TREE_CONIFER_SPRUCE
                          : HOME_TREE_CONIFER_PINE;
    }
    if (roll < 45u) return HOME_TREE_BROADLEAF_ROUND;
    if (roll < 75u) return HOME_TREE_BROADLEAF_SPREADING;
    return HOME_TREE_BROADLEAF_COLUMNAR;
}

static HomeTreeKind HomeTreeKindForBiome(int treeX, int treeZ, Biome biome)
{
    bool conifer = biome == BIOME_SNOW || biome == BIOME_MOUNTAIN;
    return HomeTreeKindForFamily(treeX, treeZ, conifer);
}

static int HomeTreeCrownRadius(HomeTreeKind kind)
{
    switch (kind) {
    case HOME_TREE_BROADLEAF_ROUND:
    case HOME_TREE_BROADLEAF_SPREADING:
    case HOME_TREE_CONIFER_SPRUCE:
        return 4;
    case HOME_TREE_BROADLEAF_COLUMNAR:
        return 2;
    case HOME_TREE_CONIFER_PINE:
        return 3;
    }
    return HOME_TREE_MIN_SPACING_RADIUS;
}

static bool HomeTreeCandidateAt(int x, int z, TerrainMode mode,
                                Biome *outBiome)
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
    if (outBiome) *outBiome = surface.biome;
    return true;
}

static unsigned int HomeTreePlacementPriority(int x, int z)
{
    return WorldHash2DBits((unsigned int)x ^ 0x68bc21ebu,
                           (unsigned int)z ^ 0x02e5be93u);
}

bool ShouldPlaceTree(int x, int z, TerrainMode mode)
{
    Biome biome = BIOME_PLAINS;
    if (!HomeTreeCandidateAt(x, z, mode, &biome)) return false;

    unsigned int priority = HomeTreePlacementPriority(x, z);
    int crownRadius = HomeTreeCrownRadius(
        HomeTreeKindForBiome(x, z, biome));
    for (int dx = -HOME_TREE_MAX_CROWN_RADIUS;
         dx <= HOME_TREE_MAX_CROWN_RADIUS; dx++) {
        for (int dz = -HOME_TREE_MAX_CROWN_RADIUS;
             dz <= HOME_TREE_MAX_CROWN_RADIUS; dz++) {
            if (dx == 0 && dz == 0) continue;
            int neighborX = x + dx;
            int neighborZ = z + dz;
            Biome neighborBiome = BIOME_PLAINS;
            if (!HomeTreeCandidateAt(neighborX, neighborZ, mode,
                                     &neighborBiome)) {
                continue;
            }
            int neighborCrownRadius = HomeTreeCrownRadius(
                HomeTreeKindForBiome(neighborX, neighborZ, neighborBiome));
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

bool CaveAt(int x, int y, int z, int height)
{
    SubsurfaceParams params = {
        .seed = WorldGetSeed(),
        .activity = 1.0f,
        .minY = 2,
        .surfaceClearance = 4,
        .aquiferLevel = 36,
        .aquiferChance = 0.68f
    };
    return SubsurfaceSampleAt(&params, x, y, z, height).cave;
}

bool CaveWaterAt(int x, int y, int z, int height)
{
    SubsurfaceParams params = {
        .seed = WorldGetSeed(),
        .activity = 1.0f,
        .minY = 2,
        .surfaceClearance = 4,
        .aquiferLevel = 36,
        .aquiferChance = 0.68f
    };
    return SubsurfaceSampleAt(&params, x, y, z, height).flooded;
}

BlockType OreAt(int x, int y, int z)
{
    unsigned int h = WorldHash3D(x, y, z);
    if (y <= 11 && (h % 281u) == 0u) return BLOCK_DIAMOND_ORE;
    if (y <= 16 && (h % 149u) == 0u) return BLOCK_GOLD_ORE;
    if (y <= 26 && (h % 71u) == 0u) return BLOCK_IRON_ORE;
    if (y <= 42 && (h % 59u) == 0u) return BLOCK_COPPER_ORE;
    if (y <= 30 && (h % 43u) == 0u) return BLOCK_COAL_ORE;
    unsigned int quartz = WorldHash3D(
        FloorDivInt(x, 3) + 719, FloorDivInt(y, 2) - 431,
        FloorDivInt(z, 3) + 283);
    if (y <= 96 && quartz % 113u == 0u) return BLOCK_QUARTZ_ORE;
    return BLOCK_STONE;
}

BlockType StoneOrCaveBlock(int x, int y, int z, int height)
{
    SubsurfaceParams params = {
        .seed = WorldGetSeed(),
        .activity = 1.0f,
        .minY = 2,
        .surfaceClearance = 4,
        .aquiferLevel = 36,
        .aquiferChance = 0.68f
    };
    SubsurfaceSample cave = SubsurfaceSampleAt(&params, x, y, z, height);
    if (cave.cave) return cave.flooded ? BLOCK_WATER : BLOCK_AIR;
    return OreAt(x, y, z);
}

bool ShouldPlacePond(int x, int z, int height)
{
    if (height > 6) return false;
    Biome biome = BiomeAt(x, z);
    if (biome == BIOME_DESERT || biome == BIOME_MOUNTAIN) return false;
    return WorldHash2D(x, z) % 97u == 0u;
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

static void SetChunkLocalBlockIfAir(Chunk *chunk, int worldX, int y,
                                    int worldZ, BlockType type)
{
    if (!InHeight(y)) return;

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(worldX, worldZ, &cx, &cz, &lx, &lz);
    if (chunk->cx == cx && chunk->cz == cz &&
        ChunkGetLocalBlock(chunk, lx, y, lz) == BLOCK_AIR) {
        ChunkSetLocalBlock(chunk, lx, y, lz, type);
    }
}

static void PlaceHomeLeafCluster(Chunk *chunk, int centerX, int centerY,
                                 int centerZ, int radiusX, int radiusY,
                                 int radiusZ, unsigned int lane)
{
    bool compactCluster = radiusX == 1 && radiusY == 1 && radiusZ == 1;
    float distanceLimit = compactCluster ? 2.05f : 1.18f;
    for (int dx = -radiusX; dx <= radiusX; dx++) {
        for (int dy = -radiusY; dy <= radiusY; dy++) {
            for (int dz = -radiusZ; dz <= radiusZ; dz++) {
                float nx = (float)dx / (float)radiusX;
                float ny = (float)dy / (float)radiusY;
                float nz = (float)dz / (float)radiusZ;
                float distance = nx * nx + ny * ny + nz * nz;
                if (distance > distanceLimit) continue;
                unsigned int edgeHash = WorldHash3D(
                    centerX + dx, centerY + dy + (int)(lane % 503u),
                    centerZ + dz);
                if (distance > 0.72f && edgeHash % 6u == 0u) continue;
                SetChunkLocalBlockIfAir(chunk, centerX + dx, centerY + dy,
                                        centerZ + dz, BLOCK_LEAVES);
            }
        }
    }
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

static void PlaceHomeBranch(Chunk *chunk, int treeX, int startY, int treeZ,
                            int direction, int reach, int rise, int bend,
                            int *outX, int *outY, int *outZ)
{
    int directionX = 0;
    int directionZ = 0;
    HomeTreeDirection(direction, &directionX, &directionZ);
    int endX = treeX;
    int endY = startY;
    int endZ = treeZ;
    for (int step = 1; step <= reach; step++) {
        endX = treeX + directionX * step;
        endY = startY + (rise * step) / reach;
        endZ = treeZ + directionZ * step;
        SetChunkLocalBlock(chunk, endX, endY, endZ, BLOCK_WOOD);
    }
    if (bend != 0) {
        endX += -directionZ * bend;
        endZ += directionX * bend;
        SetChunkLocalBlock(chunk, endX, endY, endZ, BLOCK_WOOD);
    }
    if (outX) *outX = endX;
    if (outY) *outY = endY;
    if (outZ) *outZ = endZ;
}

static void PlaceHomeBroadleafTree(Chunk *chunk, int treeX, int base,
                                   int treeZ, HomeTreeKind kind)
{
    unsigned int shapeHash = HomeTreeShapeHash(treeX, treeZ);
    int trunkHeight = 6 + (int)(shapeHash % 3u);
    int branchCount = 4 + (int)((shapeHash >> 5) % 2u);
    int baseReach = 2;
    int reachRange = 1;
    int leafRadius = 2;
    if (kind == HOME_TREE_BROADLEAF_SPREADING) {
        trunkHeight = 5 + (int)(shapeHash % 2u);
        branchCount = 5 + (int)((shapeHash >> 5) % 2u);
        baseReach = 2;
        reachRange = 2;
        leafRadius = 1;
    } else if (kind == HOME_TREE_BROADLEAF_COLUMNAR) {
        trunkHeight = 9 + (int)(shapeHash % 3u);
        branchCount = 4;
        baseReach = 1;
        reachRange = 1;
        leafRadius = 1;
    }

    for (int y = base; y < base + trunkHeight; y++) {
        SetChunkLocalBlock(chunk, treeX, y, treeZ, BLOCK_WOOD);
    }

    int trunkTop = base + trunkHeight - 1;
    int baseDirection = (int)((shapeHash >> 9) & 3u);
    for (int branch = 0; branch < branchCount; branch++) {
        unsigned int branchHash = WorldHash3D(
            treeX, 601 + branch * 17, treeZ);
        int startDepth = kind == HOME_TREE_BROADLEAF_COLUMNAR ? 4 : 3;
        int startY = trunkTop - startDepth +
                     (int)(branchHash % (unsigned int)startDepth);
        if (startY < base + 2) startY = base + 2;
        int reach = baseReach +
                    (int)((branchHash >> 3) % (unsigned int)reachRange);
        int rise = kind == HOME_TREE_BROADLEAF_SPREADING
            ? (int)((branchHash >> 6) % 2u)
            : 1 + (int)((branchHash >> 6) % 2u);
        if (kind == HOME_TREE_BROADLEAF_COLUMNAR) {
            rise = 1 + (int)((branchHash >> 6) % 3u);
        }
        int bend = kind == HOME_TREE_BROADLEAF_COLUMNAR
            ? 0 : (int)((branchHash >> 10) % 3u) - 1;
        int endX = treeX;
        int endY = startY;
        int endZ = treeZ;
        PlaceHomeBranch(chunk, treeX, startY, treeZ,
                        baseDirection + branch, reach, rise, bend,
                        &endX, &endY, &endZ);
        int leafHeight = kind == HOME_TREE_BROADLEAF_COLUMNAR ? 2 : 1;
        PlaceHomeLeafCluster(chunk, endX, endY, endZ,
                             leafRadius, leafHeight, leafRadius,
                             branchHash);
    }

    if (kind == HOME_TREE_BROADLEAF_ROUND) {
        PlaceHomeLeafCluster(chunk, treeX, trunkTop + 1, treeZ,
                             2, 2, 2, shapeHash);
    } else if (kind == HOME_TREE_BROADLEAF_SPREADING) {
        PlaceHomeLeafCluster(chunk, treeX, trunkTop + 1, treeZ,
                             3, 1, 3, shapeHash);
        PlaceHomeLeafCluster(chunk, treeX, trunkTop + 2, treeZ,
                             1, 1, 1, shapeHash >> 8);
    } else {
        PlaceHomeLeafCluster(chunk, treeX, trunkTop - 5, treeZ,
                             1, 2, 1, shapeHash >> 4);
        PlaceHomeLeafCluster(chunk, treeX, trunkTop - 2, treeZ,
                             2, 2, 2, shapeHash);
        PlaceHomeLeafCluster(chunk, treeX, trunkTop + 1, treeZ,
                             1, 2, 1, shapeHash >> 8);
    }
}

static void PlaceHomeConiferWhorl(Chunk *chunk, int treeX, int y, int treeZ,
                                  int branchLength, int ringIndex,
                                  bool sparse)
{
    unsigned int ringHash = WorldHash3D(treeX, 907 + ringIndex * 23,
                                        treeZ);
    int skippedDirection = sparse ? (int)(ringHash & 3u) : -1;
    int baseDirection = (int)((ringHash >> 3) & 3u);
    for (int branch = 0; branch < 4; branch++) {
        if (branch == skippedDirection) continue;
        unsigned int branchHash = WorldHash3D(
            treeX, 991 + ringIndex * 31 + branch * 7, treeZ);
        int direction = baseDirection + branch;
        int rise = branchLength >= 3 ? -1 : 0;
        if (sparse && branchLength == 1) rise = 1;
        int bend = (int)((branchHash >> 4) % 3u) - 1;
        int endX = treeX;
        int endY = y;
        int endZ = treeZ;
        PlaceHomeBranch(chunk, treeX, y, treeZ, direction, branchLength,
                        rise, bend, &endX, &endY, &endZ);

        int directionX = 0;
        int directionZ = 0;
        HomeTreeDirection(direction, &directionX, &directionZ);
        int sideX = -directionZ;
        int sideZ = directionX;
        for (int step = 1; step <= branchLength; step++) {
            int branchX = treeX + directionX * step;
            int branchY = y + (rise * step) / branchLength;
            int branchZ = treeZ + directionZ * step;
            SetChunkLocalBlockIfAir(chunk, branchX, branchY + 1,
                                    branchZ, BLOCK_LEAVES);
            if (step > 1 || branchLength == 1) {
                SetChunkLocalBlockIfAir(chunk, branchX + sideX, branchY,
                                        branchZ + sideZ, BLOCK_LEAVES);
                SetChunkLocalBlockIfAir(chunk, branchX - sideX, branchY,
                                        branchZ - sideZ, BLOCK_LEAVES);
            }
        }
        PlaceHomeLeafCluster(chunk, endX, endY, endZ,
                             1, 1, 1, branchHash);
    }
    PlaceHomeLeafCluster(chunk, treeX, y + 1, treeZ,
                         1, 1, 1, ringHash);
}

static void PlaceHomeConiferTree(Chunk *chunk, int treeX, int base,
                                 int treeZ, HomeTreeKind kind)
{
    unsigned int shapeHash = HomeTreeShapeHash(treeX, treeZ);
    bool sparse = kind == HOME_TREE_CONIFER_PINE;
    int trunkHeight = sparse ? 12 + (int)(shapeHash % 4u)
                             : 9 + (int)(shapeHash % 4u);
    for (int y = base; y < base + trunkHeight; y++) {
        SetChunkLocalBlock(chunk, treeX, y, treeZ, BLOCK_WOOD);
    }

    int trunkTop = base + trunkHeight - 1;
    int crownBase = base + (sparse ? 5 : 2);
    int crownSpan = trunkTop - crownBase;
    int ringIndex = 0;
    for (int y = crownBase; y < trunkTop; y += 2) {
        int remaining = trunkTop - y;
        int branchLength = sparse
            ? 1 + remaining / crownSpan
            : 1 + (remaining * 2) / crownSpan;
        PlaceHomeConiferWhorl(chunk, treeX, y, treeZ, branchLength,
                              ringIndex++, sparse);
    }
    PlaceHomeLeafCluster(chunk, treeX, trunkTop, treeZ,
                         1, 2, 1, shapeHash);
}

static void PlaceHomeTree(Chunk *chunk, int treeX, int base, int treeZ,
                          HomeTreeKind kind)
{
    if (kind == HOME_TREE_CONIFER_SPRUCE ||
        kind == HOME_TREE_CONIFER_PINE) {
        PlaceHomeConiferTree(chunk, treeX, base, treeZ, kind);
    } else {
        PlaceHomeBroadleafTree(chunk, treeX, base, treeZ, kind);
    }
}

#ifdef TERRAIN_TESTING
int TerrainTestHomeTreeVariantAt(int treeX, int treeZ, bool conifer)
{
    HomeTreeKind kind = HomeTreeKindForFamily(treeX, treeZ, conifer);
    return conifer ? (int)kind - (int)HOME_TREE_CONIFER_SPRUCE
                   : (int)kind;
}

int TerrainTestHomeTreeCrownRadiusAt(int treeX, int treeZ)
{
    HomeTreeKind kind = HomeTreeKindForBiome(
        treeX, treeZ, BiomeAt(treeX, treeZ));
    return HomeTreeCrownRadius(kind);
}

void TerrainTestPlaceHomeTree(Chunk *chunk, int treeX, int base, int treeZ,
                              bool conifer, int variant)
{
    int count = conifer ? HOME_TREE_CONIFER_VARIANT_COUNT
                        : HOME_TREE_BROADLEAF_VARIANT_COUNT;
    int normalized = variant % count;
    if (normalized < 0) normalized += count;
    HomeTreeKind kind = conifer
        ? (HomeTreeKind)(HOME_TREE_CONIFER_SPRUCE + normalized)
        : (HomeTreeKind)normalized;
    PlaceHomeTree(chunk, treeX, base, treeZ, kind);
}
#endif

static void MaterializeHomeTerrainRange(Chunk *chunk, int cx, int cz,
                                        int minY, int maxY,
                                        TerrainMode mode)
{
    int firstSectionY = SurfaceSectionYFromBlockY(minY);
    int lastSectionY = SurfaceSectionYFromBlockY(maxY);
    for (int sectionY = firstSectionY;
         sectionY <= lastSectionY; sectionY++) {
        GenerateChunkTerrainSectionBase(chunk, cx, cz, sectionY, mode);
    }
}

static void GenerateMineshaft(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    const int spacing = 40;
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(startX - 16, spacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 16, spacing);
    int minAnchorZ = FloorDivInt(startZ - 16, spacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 16, spacing);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            if (WorldHash2D(anchorX + 17, anchorZ + 29) % 100u >= 30u) continue;

            int wx = anchorX * spacing;
            int wz = anchorZ * spacing;
            int wy = 8 + (int)(WorldHash2D(anchorX + 3, anchorZ + 5) % 5u);
            int dx = (WorldHash2D(anchorX + 7, anchorZ + 11) % 2u) ? 1 : -1;
            int dz = (WorldHash2D(anchorX + 13, anchorZ + 19) % 2u) ? 1 : -1;
            int length = 12 + (int)(WorldHash2D(anchorX + 23, anchorZ + 31) % 9u);
            MaterializeHomeTerrainRange(
                chunk, cx, cz, wy, wy + 3, mode);

            for (int i = 0; i < length; i++) {
                int bx = wx + dx * i;
                int bz = wz + dz * i;
                for (int ly = 0; ly <= 2; ly++) {
                    SetChunkLocalBlock(chunk, bx, wy + ly, bz, BLOCK_AIR);
                    if (i % 3 == 0 && ly == 0) {
                        SetChunkLocalBlock(chunk, bx, wy, bz, BLOCK_AIR);
                    }
                }
                if (i % 4 == 0) {
                    for (int ly = 0; ly <= 2; ly++) {
                        SetChunkLocalBlock(chunk, bx, wy + ly, bz, BLOCK_WOOD);
                    }
                    SetChunkLocalBlock(chunk, bx, wy + 3, bz, BLOCK_WOOD);
                }
            }
        }
    }
}

static void GenerateDungeon(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    const int spacing = 80;
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(startX - 6, spacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 6, spacing);
    int minAnchorZ = FloorDivInt(startZ - 6, spacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 6, spacing);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            if (WorldHash2D(anchorX + 41, anchorZ + 53) % 100u >= 25u) continue;

            int wx = anchorX * spacing;
            int wz = anchorZ * spacing;
            int wy = 10 + (int)(WorldHash2D(anchorX + 2, anchorZ + 4) % 4u);
            MaterializeHomeTerrainRange(
                chunk, cx, cz, wy, wy + 3, mode);

            for (int ox = -3; ox <= 3; ox++) {
                for (int oz = -3; oz <= 3; oz++) {
                    bool wall = abs(ox) == 3 || abs(oz) == 3;
                    bool door = (ox >= -1 && ox <= 0) && (oz == -3);
                    int bx = wx + ox;
                    int bz = wz + oz;
                    if (wall && !door) {
                        for (int oy = 0; oy <= 3; oy++) {
                            SetChunkLocalBlock(chunk, bx, wy + oy, bz, BLOCK_STONE_BRICKS);
                        }
                    }
                }
            }
            for (int oy = 0; oy <= 3; oy++) {
                SetChunkLocalBlock(chunk, wx - 3, wy + oy, wz, BLOCK_STONE_BRICKS);
                SetChunkLocalBlock(chunk, wx + 3, wy + oy, wz, BLOCK_STONE_BRICKS);
                SetChunkLocalBlock(chunk, wx, wy + oy, wz + 3, BLOCK_STONE_BRICKS);
            }
        }
    }
}

static void GenerateDesertTemple(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    const int spacing = 90;
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(startX - 7, spacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 7, spacing);
    int minAnchorZ = FloorDivInt(startZ - 7, spacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 7, spacing);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            int wx = anchorX * spacing;
            int wz = anchorZ * spacing;
            if (BiomeAt(wx, wz) != BIOME_DESERT) continue;
            if (WorldHash2D(anchorX + 67, anchorZ + 79) % 100u >= 20u) continue;

            int base = TerrainHeight(wx, wz, mode);

            for (int ox = -2; ox <= 2; ox++) {
                for (int oz = -2; oz <= 2; oz++) {
                    for (int oy = 0; oy <= 2; oy++) {
                        SetChunkLocalBlock(chunk, wx + ox, base + 1 + oy, wz + oz, BLOCK_SANDSTONE);
                    }
                }
            }
            SetChunkLocalBlock(chunk, wx, base + 4, wz, BLOCK_SANDSTONE);

            for (int oy = 1; oy <= 4; oy++) {
                SetChunkLocalBlock(chunk, wx - 4, base + oy, wz, BLOCK_SANDSTONE);
                SetChunkLocalBlock(chunk, wx + 4, base + oy, wz, BLOCK_SANDSTONE);
                SetChunkLocalBlock(chunk, wx, base + oy, wz - 4, BLOCK_SANDSTONE);
                SetChunkLocalBlock(chunk, wx, base + oy, wz + 4, BLOCK_SANDSTONE);
            }
            for (int ox = -1; ox <= 1; ox++) {
                for (int oz = -1; oz <= 1; oz++) {
                    for (int oy = 0; oy <= 3; oy++) {
                        SetChunkLocalBlock(chunk, wx + ox, base - 1 - oy, wz + oz, BLOCK_SANDSTONE);
                    }
                }
            }
        }
    }
}
#define VILLAGE_SPACING 48
#define VILLAGE_PROBABILITY 35u
#define VILLAGE_HALF_WIDTH 3
#define VILLAGE_HALF_DEPTH 2

static void SetVillageBlock(Chunk *chunk, int x, int y, int z, BlockType type)
{
    SetChunkLocalBlock(chunk, x, y, z, type);
}

static void GenerateVillage(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;

    int minAnchorX = FloorDivInt(startX - VILLAGE_HALF_WIDTH - 1, VILLAGE_SPACING);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + VILLAGE_HALF_WIDTH, VILLAGE_SPACING);
    int minAnchorZ = FloorDivInt(startZ - VILLAGE_HALF_DEPTH - 1, VILLAGE_SPACING);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + VILLAGE_HALF_DEPTH, VILLAGE_SPACING);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            int wx = anchorX * VILLAGE_SPACING;
            int wz = anchorZ * VILLAGE_SPACING;
            if (WorldHash2D(anchorX, anchorZ) % 100u >= VILLAGE_PROBABILITY) continue;
            if (BiomeAt(wx, wz) == BIOME_DESERT) continue;
            if (TerrainHeight(wx, wz, mode) < 4) continue;

            int minX = wx - VILLAGE_HALF_WIDTH;
            int maxX = wx + VILLAGE_HALF_WIDTH;
            int minZ = wz - VILLAGE_HALF_DEPTH;
            int maxZ = wz + VILLAGE_HALF_DEPTH;
            int base = TerrainHeight(wx, wz, mode);

            for (int x = minX; x <= maxX; x++) {
                for (int z = minZ; z <= maxZ; z++) {
                    if (x < startX || x >= startX + CHUNK_SIZE ||
                        z < startZ || z >= startZ + CHUNK_SIZE) continue;

                    for (int y = 0; y <= 5; y++) {
                        BlockType type = BLOCK_AIR;
                        bool edge = x == minX || x == maxX || z == minZ || z == maxZ;
                        bool corner = (x == minX || x == maxX) && (z == minZ || z == maxZ);
                        bool front = z == maxZ;

                        if (y == 0) {
                            type = BLOCK_PLANK;
                        } else if (y >= 1 && y <= 2 && edge) {
                            if (front && (x == wx - 1 || x == wx + 0)) type = BLOCK_AIR;
                            else if (y == 2 && edge && (x == wx || z == wz) &&
                                     !(z == minZ || z == maxZ) && x != minX && x != maxX) {
                                type = BLOCK_GLASS;
                            } else {
                                type = corner ? BLOCK_WOOD : BLOCK_PLANK;
                            }
                        } else if (y == 3) {
                            type = edge ? BLOCK_PLANK : BLOCK_AIR;
                        } else if (y == 4) {
                            type = BLOCK_AIR;
                            if (x > minX && x < maxX && z > minZ && z < maxZ) type = BLOCK_PLANK;
                        } else if (y == 5) {
                            type = BLOCK_AIR;
                            if (x >= minX + 2 && x <= maxX - 2 && z == wz) type = BLOCK_PLANK;
                        }
                        if (type != BLOCK_AIR) SetVillageBlock(chunk, x, base + y, z, type);
                    }
                }
            }
        }
    }
}

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

static void PlacePlanetForest(Chunk *chunk, int treeX, int treeZ)
{
    if (PlanetBiomeAt(treeX, treeZ) != PLANET_BIOME_FOREST) return;
    if (PlanetHash2D(treeX, treeZ, 151u) % 59u != 0u) return;
    PlanetEcologySuitability suitability = PlanetEcologyStaticSuitabilityAt(
        treeX, treeZ);
    float ecologyRoll = (float)(PlanetHash2D(treeX, treeZ, 173u) & 0x00ffffffu) /
                        16777215.0f;
    if (ecologyRoll > suitability.floraCapacity) return;

    int base = PlanetTerrainHeight(treeX, treeZ) + 1;
    if (base <= PlanetSeaLevelForProfile(PlanetWorldStyle(), PlanetWorldProfile())) return;
    int trunkHeight = 4 + (int)(PlanetHash2D(treeX, treeZ, 163u) % 3u);
    for (int y = base; y < base + trunkHeight && InHeight(y); y++) {
        SetChunkLocalBlock(chunk, treeX, y, treeZ, BLOCK_WOOD);
    }
    for (int ox = -2; ox <= 2; ox++) {
        for (int oz = -2; oz <= 2; oz++) {
            for (int oy = trunkHeight - 2; oy <= trunkHeight; oy++) {
                if (abs(ox) + abs(oz) + (oy == trunkHeight ? 1 : 0) > 4) continue;
                SetChunkLocalBlock(chunk, treeX + ox, base + oy, treeZ + oz, BLOCK_LEAVES);
            }
        }
    }
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
                       decor % (biome == PLANET_BIOME_FOREST ? 61u : 97u) == 0u &&
                       ((float)(PlanetHash2D(worldX, worldZ, 419u) & 0x00ffffffu) /
                        16777215.0f) <= PlanetEcologyStaticSuitabilityAt(
                            worldX, worldZ).floraCapacity) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_FLOWER);
            }
        }
    }

    for (int treeX = startX - 2; treeX < startX + CHUNK_SIZE + 2; treeX++) {
        for (int treeZ = startZ - 2; treeZ < startZ + CHUNK_SIZE + 2; treeZ++) {
            PlacePlanetForest(chunk, treeX, treeZ);
        }
    }
    PlanetEcologyApplyToChunk(chunk, cx, cz);
    PlanetPoiApplyToChunk(chunk, cx, cz);
}

static bool HomeRedSandAt(int worldX, int worldZ)
{
    return WorldHash2D(FloorDivInt(worldX, 10) + 947,
                       FloorDivInt(worldZ, 10) - 947) % 3u == 0u;
}

static bool HomeWetSoilAt(const SurfaceTerrainSample *surface)
{
    if (!surface) return false;
    return (surface->biome == BIOME_PLAINS ||
            surface->biome == BIOME_FOREST) &&
           surface->elevation <= (float)HOME_SEA_LEVEL + 6.0f &&
           surface->continentalness < 0.56f;
}

static float HomeGeologyField(int worldX, int y, int worldZ,
                              unsigned int lane)
{
    float fx = (float)worldX * 0.0085f + (float)y * 0.0041f;
    float fz = (float)worldZ * 0.0085f - (float)y * 0.0037f;
    return WorldValueNoise2D(fx, fz, lane);
}

static bool HomeSaltDepositAt(int worldX, int worldZ)
{
    return WorldValueNoise2D((float)worldX * 0.010f,
                             (float)worldZ * 0.010f, 887u) > 0.74f;
}

static bool HomePeatDepositAt(int worldX, int worldZ)
{
    return WorldValueNoise2D((float)worldX * 0.014f,
                             (float)worldZ * 0.014f, 941u) > 0.48f;
}

static BlockType HomeRockBlock(int worldX, int y, int worldZ, int height,
                               Biome biome)
{
    BlockType type = StoneOrCaveBlock(worldX, y, worldZ, height);
    int depth = height - y;
    if (type != BLOCK_STONE) return type;
    return TerrainGeologyHomeStoneBlock(
        biome, depth, HomeGeologyField(worldX, y, worldZ, 733u),
        HomeGeologyField(worldX, y, worldZ, 811u));
}

static BlockType HomeTerrainBaseBlockFromSample(
    int worldX, int y, int worldZ, TerrainMode mode,
    const SurfaceTerrainSample *surface, int seaLevel)
{
    if (!surface || !InHeight(y) ||
        y >= SURFACE_GENERATION_MAX_Y_EXCLUSIVE ||
        (mode == TERRAIN_FLAT && y < SURFACE_GENERATION_MIN_Y)) {
        return BLOCK_AIR;
    }
    int height = (int)lroundf(surface->elevation);
    Biome biome = surface->biome;
    bool submerged = seaLevel >= 0 && height < seaLevel;
    bool redSand = biome == BIOME_DESERT &&
                   HomeRedSandAt(worldX, worldZ);
    bool wetSoil = HomeWetSoilAt(surface);
    bool peat = y <= height && wetSoil && y >= height - 4 &&
                HomePeatDepositAt(worldX, worldZ);
    bool salt = y <= height && (biome == BIOME_DESERT || submerged) &&
                y >= height - 6 &&
                HomeSaltDepositAt(worldX, worldZ);

    if (y > height) {
        if (!submerged || y > seaLevel) return BLOCK_AIR;
        return biome == BIOME_SNOW && y == seaLevel
            ? BLOCK_ICE : BLOCK_WATER;
    }

    BlockType type = BLOCK_STONE;
    if (mode == TERRAIN_FLAT) {
        type = y == height ? BLOCK_GRASS : BLOCK_DIRT;
    } else if (y == SURFACE_MIN_Y || (!submerged && y == 0)) {
        type = BLOCK_BEDROCK;
    } else if (submerged) {
        int sedimentDepth = 2;
        if (surface->bathymetry.zone == BATHYMETRY_ZONE_COAST ||
            surface->bathymetry.zone == BATHYMETRY_ZONE_SHELF) {
            sedimentDepth = 6;
        } else if (surface->bathymetry.zone == BATHYMETRY_ZONE_SLOPE ||
                   surface->bathymetry.zone ==
                       BATHYMETRY_ZONE_ABYSSAL_PLAIN) {
            sedimentDepth = 4;
        }
        if (y > height - sedimentDepth) {
            type = BathymetryMaterialBlock(surface->bathymetry.material);
            if ((surface->bathymetry.zone == BATHYMETRY_ZONE_COAST ||
                 surface->bathymetry.zone == BATHYMETRY_ZONE_SHELF) &&
                surface->bathymetry.material == BATHYMETRY_MATERIAL_SAND &&
                salt) {
                type = BLOCK_ROCK_SALT;
            } else if ((surface->bathymetry.zone == BATHYMETRY_ZONE_COAST ||
                        surface->bathymetry.zone == BATHYMETRY_ZONE_SHELF) &&
                surface->bathymetry.material == BATHYMETRY_MATERIAL_SAND &&
                WorldHash2D(FloorDivInt(worldX, 6) + 313,
                            FloorDivInt(worldZ, 6) - 197) % 5u == 0u) {
                type = BLOCK_CLAY;
            }
        } else {
            type = HomeRockBlock(worldX, y, worldZ, height, biome);
        }
    } else if (y < height) {
        if (biome == BIOME_DESERT) {
            type = y > height - 3
                ? (salt ? BLOCK_ROCK_SALT
                        : (redSand ? BLOCK_RED_SAND : BLOCK_SAND))
                : HomeRockBlock(worldX, y, worldZ, height, biome);
        } else if (biome == BIOME_SNOW) {
            type = y > height - 3
                ? BLOCK_PERMAFROST
                : HomeRockBlock(worldX, y, worldZ, height, biome);
        } else if (biome == BIOME_MOUNTAIN) {
            type = y > height - 4 && height < 24
                ? BLOCK_DIRT
                : HomeRockBlock(worldX, y, worldZ, height, biome);
        } else {
            type = y > height - 4
                ? (peat ? BLOCK_PEAT : (wetSoil ? BLOCK_MUD : BLOCK_DIRT))
                : HomeRockBlock(worldX, y, worldZ, height, biome);
        }
    } else if (biome == BIOME_DESERT) {
        type = salt ? BLOCK_ROCK_SALT
                    : (redSand ? BLOCK_RED_SAND : BLOCK_SAND);
    } else if (biome == BIOME_SNOW) {
        type = BLOCK_SNOW;
    } else if (biome == BIOME_MOUNTAIN) {
        type = height >= 165 ? BLOCK_SNOW
                             : (height >= 125 ? BLOCK_STONE : BLOCK_GRASS);
    } else {
        type = peat ? BLOCK_PEAT : (wetSoil ? BLOCK_MUD : BLOCK_GRASS);
    }

    if (mode != TERRAIN_FLAT && y == height - 2 &&
        CaveWaterAt(worldX, y, worldZ, height) &&
        CaveAt(worldX, y, worldZ, height) &&
        !CaveAt(worldX, y - 1, worldZ, height)) {
        return BLOCK_WATER;
    }
    return type;
}

BlockType TerrainBaseBlockAt(int x, int y, int z, TerrainMode mode)
{
    if (!InHeight(y) || y >= SURFACE_GENERATION_MAX_Y_EXCLUSIVE ||
        (mode == TERRAIN_FLAT && y < SURFACE_GENERATION_MIN_Y)) {
        return BLOCK_AIR;
    }
    SurfaceTerrainSample surface = SurfaceTerrainAt(x, z, mode);
    return HomeTerrainBaseBlockFromSample(
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
                BlockType type = HomeTerrainBaseBlockFromSample(
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
                (WorldHash2D(worldX, worldZ) % 23u) == 0u) {
                int cactusHeight = 2 + (int)(WorldHash2D(worldX, worldZ + 13) % 2u);
                for (int y = height + 1; y < height + 1 + cactusHeight && InHeight(y); y++) {
                    SetChunkLocalBlock(chunk, worldX, y, worldZ, BLOCK_CACTUS);
                }
            }
        }
    }

    if (mode != TERRAIN_FLAT) {
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int worldX = startX + lx;
                int worldZ = startZ + lz;
                int height = (int)lroundf(samples[lx][lz].elevation);
                if (height < 4) continue;
                BlockType surface = ChunkGetLocalBlock(chunk, lx, height, lz);
                if (surface != BLOCK_GRASS) continue;
                unsigned int h = WorldHash2D(worldX, worldZ);
                if (h % 173u == 0u) {
                    SetChunkLocalBlock(chunk, worldX, height + 1, worldZ, BLOCK_FLOWER);
                } else if (h % 397u == 0u) {
                    SetChunkLocalBlock(chunk, worldX, height + 1, worldZ, BLOCK_MUSHROOM);
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
            Biome treeBiome = BiomeAt(treeX, treeZ);
            HomeTreeKind kind = HomeTreeKindForBiome(
                treeX, treeZ, treeBiome);
            PlaceHomeTree(chunk, treeX, base, treeZ, kind);
        }
    }

    if (mode != TERRAIN_FLAT) {
        GenerateVillage(chunk, cx, cz, mode);
        GenerateMineshaft(chunk, cx, cz, mode);
        GenerateDungeon(chunk, cx, cz, mode);
        GenerateDesertTemple(chunk, cx, cz, mode);
    }
}

#ifdef TERRAIN_TESTING
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

void TerrainTestGenerateMineshaft(Chunk *chunk, int cx, int cz,
                                  TerrainMode mode)
{
    GenerateMineshaft(chunk, cx, cz, mode);
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
