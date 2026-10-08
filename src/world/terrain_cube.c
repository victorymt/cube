#include "world/terrain_cube.h"

#include "world/planet_landform_strategy.h"
#include "world/terrain_geology_internal.h"
#include "world/subsurface.h"
#include "raymath.h"

#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>

struct TerrainCubeContext {
    WorldSurfaceSnapshot snapshot;
    TerrainMode mode;
    uint64_t generation;
    PlanetLandformStrategy strategy;
    LandformAtlas *atlas;
    SubsurfaceParams subsurface;
    pthread_mutex_t referencesMutex;
    unsigned int references;
};

static float HomeShellElevation(float elevation)
{
    const float shelfBottom = HOME_SEA_LEVEL - 200;
    const float deepest = TERRAIN_CUBE_MIN_RADIAL + 32;
    if (elevation >= shelfBottom) return elevation;
    float fraction = (shelfBottom - elevation) / (HOME_BATHYMETRY_MAX_WATER_DEPTH - 200);
    return shelfBottom + Clamp(fraction, 0, 1)*(deepest - shelfBottom);
}

static LandformBaseline CubeBaseline(Vector3 direction, void *user)
{
    const TerrainCubeContext *context = user;
    uint32_t seed = context->snapshot.context.seed;
    if (context->snapshot.context.kind == WORLD_SURFACE_HOME) {
        SurfaceTerrainSample sample = {0};
        float elevation = HomeShellElevation(TerrainHomeBaselineAtDirection(seed, direction, &sample));
        float latitude = fabsf(direction.y);
        float rain = .65f - .56f*expf(-powf((latitude - .45f)/.19f, 2));
        return (LandformBaseline){elevation, rain, .25f + .6f*sample.ridge};
    }
    PlanetSurfaceSample sample = {0};
    float elevation = TerrainPlanetBaselineAtGeographic(seed, &context->snapshot.planetProfile,
        atan2f(direction.z, direction.x), atan2f(direction.y, hypotf(direction.x, direction.z)), &sample);
    float warmth = Clamp((sample.meanTemperature - 263.15f)/20, 0, 1);
    return (LandformBaseline){elevation,
        Clamp(sample.moisture*warmth*(1 - sample.iceCoverage), 0, 1),
        Clamp(.25f + .6f*sample.ridge, 0, 1)};
}

TerrainCubeContext *TerrainCubeContextCreate(const WorldSurfaceSnapshot *snapshot,
                                             TerrainMode mode, uint64_t generation)
{
    if (!snapshot || !snapshot->context.active ||
        (snapshot->context.kind != WORLD_SURFACE_HOME && snapshot->context.kind != WORLD_SURFACE_PLANET) ||
        snapshot->context.topology.kind != SURFACE_TOPOLOGY_CUBE_GRID ||
        snapshot->context.topology.version != SURFACE_CUBE_TOPOLOGY_VERSION ||
        (mode != TERRAIN_FLAT && mode != TERRAIN_VARIED) ||
        (snapshot->context.kind == WORLD_SURFACE_PLANET && !snapshot->planetProfile.hasSolidSurface)) return NULL;
    TerrainCubeContext *context = calloc(1, sizeof(*context));
    if (!context) return NULL;
    context->snapshot = *snapshot;
    context->mode = mode;
    context->generation = generation;
    context->references = 1;
    if (pthread_mutex_init(&context->referencesMutex, NULL) != 0) { free(context); return NULL; }
    context->strategy = PlanetLandformStrategyForSurface(snapshot->context.kind,
        snapshot->context.seed, &snapshot->planetProfile);
    context->subsurface = (SubsurfaceParams){snapshot->context.seed, 1, TERRAIN_CUBE_MIN_RADIAL + 2,
        4, 36, .55f};
    if (snapshot->context.kind == WORLD_SURFACE_PLANET) {
        const PlanetProfile *p = &snapshot->planetProfile;
        context->subsurface.activity = Clamp(.72f + p->terrainRoughness*.38f, .55f, 1.35f);
        context->subsurface.aquiferChance = Clamp(p->oceanCoverage*.95f, 0, .88f);
        if (p->style == SOLAR_STYLE_LAVA) {
            context->subsurface.activity = fmaxf(context->subsurface.activity, 1.18f);
            context->subsurface.aquiferLevel = 42;
            context->subsurface.aquiferChance = .72f;
        } else if (p->style == SOLAR_STYLE_ICE) {
            context->subsurface.activity *= .88f;
            context->subsurface.aquiferLevel = 32;
            context->subsurface.aquiferChance = .54f;
        } else if (p->style == SOLAR_STYLE_DESERT) {
            context->subsurface.activity *= .94f;
            context->subsurface.aquiferChance *= .22f;
        } else if (p->style == SOLAR_STYLE_CRATER) {
            context->subsurface.activity = fmaxf(context->subsurface.activity, 1.05f);
            context->subsurface.aquiferChance *= .12f;
        }
    }
    if (mode != TERRAIN_FLAT && (context->strategy.capabilities & LANDFORM_HAS_DRAINAGE)) {
        LandformBuildInput build = PlanetLandformBuildInput(&context->strategy);
        build.baseline = CubeBaseline;
        build.user = context;
        context->atlas = LandformBuildWithInput(&build);
        if (!context->atlas) { TerrainCubeContextRelease(context); return NULL; }
        context->atlas->terraces = true;
    }
    return context;
}

TerrainCubeContext *TerrainCubeContextRetain(TerrainCubeContext *context)
{
    if (!context) return NULL;
    pthread_mutex_lock(&context->referencesMutex);
    bool valid = context->references < UINT_MAX;
    if (valid) context->references++;
    pthread_mutex_unlock(&context->referencesMutex);
    return valid ? context : NULL;
}

void TerrainCubeContextRelease(TerrainCubeContext *context)
{
    if (!context) return;
    pthread_mutex_lock(&context->referencesMutex);
    bool destroy = --context->references == 0;
    pthread_mutex_unlock(&context->referencesMutex);
    if (destroy) {
        LandformDestroy(context->atlas);
        pthread_mutex_destroy(&context->referencesMutex);
        free(context);
    }
}

const WorldSurfaceSnapshot *TerrainCubeContextSnapshot(const TerrainCubeContext *context)
{ return context ? &context->snapshot : NULL; }
uint64_t TerrainCubeContextGeneration(const TerrainCubeContext *context)
{ return context ? context->generation : 0; }

bool TerrainCubeSampleColumn(const TerrainCubeContext *context,
                              SurfaceAddress address, TerrainCubeColumn *out)
{
    if (!context || !out || !SurfaceAddressIsValid(address) ||
        address.bodyId != context->snapshot.context.bodyId) return false;
    address.radial = 0;
    TerrainCubeColumn column = {.owner = context, .address = address, .generation = context->generation};
    column.direction = SurfaceAddressDirection(address);
    SurfaceTerrainSample *s = &column.terrain;
    uint32_t seed = context->snapshot.context.seed;
    bool home = context->snapshot.context.kind == WORLD_SURFACE_HOME;
    if (context->mode == TERRAIN_FLAT) {
        s->elevation = 8; s->seaLevel = -1; s->continentalness = 1;
        s->biome = BIOME_PLAINS;
    } else if (home) {
        s->elevation = HomeShellElevation(TerrainHomeBaselineAtDirection(seed, column.direction, s));
        s->seaLevel = HOME_SEA_LEVEL;
        float climate = TerrainDirectionalNoise(seed, column.direction, .0018f, 503u);
        s->biome = s->elevation >= 132 || s->ridge > .58f ? BIOME_MOUNTAIN
            : climate < .25f ? BIOME_SNOW : climate < .43f ? BIOME_DESERT
            : climate > .64f ? BIOME_FOREST : BIOME_PLAINS;
    } else {
        const PlanetProfile *p = &context->snapshot.planetProfile;
        s->elevation = TerrainPlanetBaselineAtGeographic(seed, p,
            atan2f(column.direction.z, column.direction.x),
            atan2f(column.direction.y, hypotf(column.direction.x, column.direction.z)), &column.planet);
        s->seaLevel = (float)PlanetSeaLevelForProfile(p->style, p);
        s->continentalness = column.planet.continentalness;
        s->erosion = column.planet.erosion;
        s->ridge = column.planet.ridge;
        s->peak = column.planet.peak;
        s->trench = column.planet.trench;
    }
    s->landform.elevation = s->elevation;
    if (context->atlas) {
        s->landform = LandformSampleAt(context->atlas, column.direction, s->elevation);
        if (!(context->strategy.capabilities & LANDFORM_HAS_SEDIMENT)) s->landform.sedimentDepth = 0;
        s->elevation = s->landform.elevation;
        s->slope = s->landform.slope;
    }
    s->elevation = Clamp(s->elevation, TERRAIN_CUBE_MIN_RADIAL + 2, SURFACE_GENERATION_MAX_Y_EXCLUSIVE - 2);
    s->bathymetry.seaLevel = (int)s->seaLevel;
    s->bathymetry.seabedY = (int)lroundf(s->elevation);
    s->bathymetry.waterDepth = s->seaLevel >= 0 ? (int)fmaxf(0, s->seaLevel - s->elevation) : 0;
    int depth = s->bathymetry.waterDepth;
    s->bathymetry.zone = depth == 0 ? BATHYMETRY_ZONE_LAND : depth <= 6 ? BATHYMETRY_ZONE_COAST
        : depth <= 20 ? BATHYMETRY_ZONE_SHELF : depth <= 200 ? BATHYMETRY_ZONE_SLOPE : BATHYMETRY_ZONE_ABYSSAL_PLAIN;
    s->bathymetry.material = depth <= 20 ? BATHYMETRY_MATERIAL_SAND : BATHYMETRY_MATERIAL_SEDIMENT;
    if (home && s->biome == BIOME_PLAINS && !depth && s->elevation <= s->seaLevel + 12 &&
        s->slope <= 2.4f && TerrainDirectionalNoise(seed, column.direction, .0024f, 607u) > .55f)
        s->biome = BIOME_SWAMP;
    *out = column;
    return true;
}

bool TerrainCubeBlock(const TerrainCubeContext *context, const TerrainCubeColumn *column,
                       int radial, BlockType *out)
{
    if (!context || !column || !out || column->owner != context ||
        column->address.bodyId != context->snapshot.context.bodyId ||
        column->generation != context->generation || !SurfaceAddressIsValid(column->address) ||
        radial < TERRAIN_CUBE_MIN_RADIAL || radial >= SURFACE_MAX_Y_EXCLUSIVE) return false;
    const SurfaceTerrainSample *s = &column->terrain;
    int height = (int)lroundf(s->elevation), depth = height - radial;
    bool home = context->snapshot.context.kind == WORLD_SURFACE_HOME;
    SolarBodyStyle style = context->snapshot.planetProfile.style;
    SurfaceAddress cell = column->address; cell.radial = radial;
    uint32_t hash = SurfaceCubeCellHash(cell) ^ context->snapshot.context.seed;
    BlockType type = BLOCK_AIR;
    if (radial == TERRAIN_CUBE_MIN_RADIAL) type = BLOCK_BEDROCK;
    else if (depth < 0) {
        if (s->seaLevel >= 0 && radial <= (int)s->seaLevel) {
            type = !home && style == SOLAR_STYLE_LAVA ? BLOCK_LAVA : BLOCK_WATER;
            if (radial == (int)s->seaLevel && ((home && s->biome == BIOME_SNOW) ||
                (!home && (style == SOLAR_STYLE_ICE || column->planet.iceCoverage > .64f)))) type = BLOCK_ICE;
        } else if (s->landform.hasWater && radial <= (int)floorf(s->landform.waterLevel)) type = BLOCK_WATER;
    } else if (context->mode == TERRAIN_FLAT) type = depth == 0 ? BLOCK_GRASS : BLOCK_DIRT;
    else if (s->landform.sedimentDepth > 0 && depth <= (int)ceilf(s->landform.sedimentDepth)) {
        float choice = (float)(hash & 65535u)/65536;
        type = choice < s->landform.gravel ? BLOCK_GRAVEL
            : choice < s->landform.gravel + s->landform.sand ? BLOCK_SAND
            : s->landform.hasWater ? BLOCK_MUD : BLOCK_SILT;
    } else {
        if (!home) type = TerrainGeologyPlanetSubsurfaceBlock(style, column->planet.biome, depth, hash);
        else if (depth < 4) type = s->biome == BIOME_DESERT ? BLOCK_SAND
            : s->biome == BIOME_SNOW ? (depth ? BLOCK_PERMAFROST : BLOCK_SNOW)
            : s->biome == BIOME_SWAMP ? BLOCK_MUD
            : depth ? BLOCK_DIRT : s->biome == BIOME_MOUNTAIN ? BLOCK_STONE : BLOCK_GRASS;
        else type = TerrainGeologyHomeStoneBlock(s->biome, depth,
            TerrainDirectionalNoise(context->snapshot.context.seed, column->direction, .003f, 701u),
            (float)(hash & 65535u)/65536);
        if (s->bathymetry.waterDepth > 0 && depth < 4 && (home || (style != SOLAR_STYLE_LAVA && style != SOLAR_STYLE_ICE)))
            type = BathymetryMaterialBlock(s->bathymetry.material);
        SubsurfaceSample cave = SubsurfaceSampleAtDirection(&context->subsurface, column->direction, radial, height);
        if (cave.cave) type = cave.flooded ? (!home && style == SOLAR_STYLE_LAVA ? BLOCK_LAVA : BLOCK_WATER) : BLOCK_AIR;
    }
    *out = type;
    return true;
}

bool TerrainCubeGenerateSection(const TerrainCubeContext *context,
                                 SurfaceCubeChunkKey key, int sectionY,
                                 unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE])
{
    if (!context || !blocks || !SurfaceCubeChunkKeyIsValid(key) ||
        key.bodyId != context->snapshot.context.bodyId ||
        sectionY < TERRAIN_CUBE_MIN_RADIAL / SURFACE_SECTION_HEIGHT ||
        sectionY >= SURFACE_MAX_Y_EXCLUSIVE / SURFACE_SECTION_HEIGHT) return false;
    for (int u = 0; u < CHUNK_SIZE; u++) for (int v = 0; v < CHUNK_SIZE; v++) {
        TerrainCubeColumn column;
        if (!TerrainCubeSampleColumn(context,
            (SurfaceAddress){key.bodyId, key.face, key.u*CHUNK_SIZE + u, key.v*CHUNK_SIZE + v, 0}, &column)) return false;
        for (int y = 0; y < SURFACE_SECTION_HEIGHT; y++) {
            BlockType type;
            if (!TerrainCubeBlock(context, &column, sectionY*SURFACE_SECTION_HEIGHT + y, &type)) return false;
            blocks[u][y][v] = (unsigned short)type;
        }
    }
    return true;
}
