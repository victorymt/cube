#include "presentation/homeworld_map_model.h"
#include "world/terrain.h"

#include <math.h>
#include <stddef.h>

static float MapClamp(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static unsigned char MapChannel(float value)
{
    return (unsigned char)MapClamp(roundf(value), 0.0f, 255.0f);
}

static Color MapScaleColor(Color color, float scale)
{
    return (Color){
        MapChannel((float)color.r * scale),
        MapChannel((float)color.g * scale),
        MapChannel((float)color.b * scale),
        color.a
    };
}

float HomeWorldMapSpanForLevel(int level)
{
    static const float spans[HOMEWORLD_MAP_ZOOM_LEVELS] = {
        256.0f, 512.0f, 1024.0f, 2048.0f
    };
    if (level < 0) level = 0;
    if (level >= HOMEWORLD_MAP_ZOOM_LEVELS) {
        level = HOMEWORLD_MAP_ZOOM_LEVELS - 1;
    }
    return spans[level];
}

Vector2 HomeWorldMapWorldToScreen(HomeWorldMapBounds bounds, Rectangle map,
                                  float worldX, float worldZ)
{
    float span = bounds.span > 0.0f ? bounds.span : 1.0f;
    return (Vector2){
        map.x + map.width * (0.5f + (worldX - bounds.centerX) / span),
        map.y + map.height * (0.5f + (worldZ - bounds.centerZ) / span)
    };
}

Vector2 HomeWorldMapScreenToWorld(HomeWorldMapBounds bounds, Rectangle map,
                                  Vector2 screen)
{
    float width = map.width > 0.0f ? map.width : 1.0f;
    float height = map.height > 0.0f ? map.height : 1.0f;
    return (Vector2){
        bounds.centerX + ((screen.x - map.x) / width - 0.5f) * bounds.span,
        bounds.centerZ + ((screen.y - map.y) / height - 0.5f) * bounds.span
    };
}

bool HomeWorldMapWorldVisible(HomeWorldMapBounds bounds, float worldX,
                              float worldZ)
{
    float half = bounds.span * 0.5f;
    return worldX >= bounds.centerX - half &&
           worldX <= bounds.centerX + half &&
           worldZ >= bounds.centerZ - half &&
           worldZ <= bounds.centerZ + half;
}

Color HomeWorldMapTerrainColor(HomeWorldMapTerrainCell cell)
{
    Color base = { 86, 132, 76, 255 };
    if (cell.planetSurface && cell.planetBiome == PLANET_BIOME_LAVA_SEA) {
        float depth = MapClamp(
            log1pf((float)cell.waterDepth) /
                log1pf((float)BATHYMETRY_MAX_WATER_DEPTH),
            0.0f, 1.0f);
        base = (Color){
            MapChannel(224.0f - depth * 112.0f),
            MapChannel(76.0f - depth * 50.0f),
            MapChannel(24.0f - depth * 16.0f),
            255
        };
    } else if (cell.waterDepth > 0 && cell.planetSurface &&
               (cell.planetBiome == PLANET_BIOME_ICE_SHEET ||
                cell.planetBiome == PLANET_BIOME_GLACIER)) {
        float depth = MapClamp(
            log1pf((float)cell.waterDepth) /
                log1pf((float)BATHYMETRY_MAX_WATER_DEPTH),
            0.0f, 1.0f);
        base = (Color){
            MapChannel(204.0f - depth * 68.0f),
            MapChannel(228.0f - depth * 64.0f),
            MapChannel(235.0f - depth * 52.0f),
            255
        };
    } else if (cell.waterDepth > 0) {
        int maxDepth = cell.planetSurface
            ? BATHYMETRY_MAX_WATER_DEPTH
            : HOME_BATHYMETRY_MAX_WATER_DEPTH;
        float depth = MapClamp(
            log1pf((float)cell.waterDepth) /
                log1pf((float)maxDepth),
            0.0f, 1.0f);
        base = (Color){
            MapChannel(44.0f - depth * 38.0f),
            MapChannel(122.0f - depth * 96.0f),
            MapChannel(164.0f - depth * 99.0f),
            255
        };
    } else if (cell.planetSurface) {
        switch (cell.planetBiome) {
        case PLANET_BIOME_BASALT_PLAINS:
            base = (Color){ 74, 72, 70, 255 }; break;
        case PLANET_BIOME_LAVA_SEA:
            base = (Color){ 214, 69, 22, 255 }; break;
        case PLANET_BIOME_VOLCANIC_RIDGE:
            base = (Color){ 105, 73, 65, 255 }; break;
        case PLANET_BIOME_ICE_SHEET:
            base = (Color){ 219, 231, 235, 255 }; break;
        case PLANET_BIOME_GLACIER:
            base = (Color){ 166, 211, 224, 255 }; break;
        case PLANET_BIOME_DUNES:
            base = (Color){ 203, 171, 104, 255 }; break;
        case PLANET_BIOME_BADLANDS:
            base = (Color){ 166, 100, 74, 255 }; break;
        case PLANET_BIOME_OASIS:
            base = (Color){ 57, 126, 83, 255 }; break;
        case PLANET_BIOME_IMPACT_BASIN:
            base = (Color){ 96, 96, 94, 255 }; break;
        case PLANET_BIOME_CRATER_HIGHLANDS:
            base = (Color){ 137, 134, 128, 255 }; break;
        case PLANET_BIOME_OCEAN:
            base = (Color){ 49, 105, 137, 255 }; break;
        case PLANET_BIOME_COAST:
            base = (Color){ 196, 181, 126, 255 }; break;
        case PLANET_BIOME_FOREST:
            base = (Color){ 39, 91, 54, 255 }; break;
        case PLANET_BIOME_ALPINE:
            base = (Color){ 145, 151, 151, 255 }; break;
        case PLANET_BIOME_STORM_BANDS:
            base = (Color){ 183, 150, 113, 255 }; break;
        case PLANET_BIOME_PLAINS:
        default:
            base = (Color){ 92, 136, 80, 255 }; break;
        }
    } else {
        switch (cell.biome) {
        case BIOME_FOREST: base = (Color){ 44, 102, 60, 255 }; break;
        case BIOME_DESERT: base = (Color){ 184, 156, 88, 255 }; break;
        case BIOME_SNOW: base = (Color){ 202, 216, 218, 255 }; break;
        case BIOME_MOUNTAIN: base = (Color){ 118, 116, 108, 255 }; break;
        case BIOME_PLAINS:
        default: base = (Color){ 89, 143, 76, 255 }; break;
        }
    }

    float elevationReference = cell.seaLevel >= 0.0f
        ? cell.seaLevel : 84.0f;
    float elevationScale = cell.planetSurface ? 96.0f : 72.0f;
    float relative = MapClamp(
        (cell.elevation - elevationReference) / elevationScale,
                              -0.45f, 0.75f);
    float shade = 1.0f + relative * 0.22f - MapClamp(cell.slope, 0.0f, 12.0f) * 0.012f;
    int contour = (int)floorf(cell.elevation);
    int modulo = contour % 8;
    if (modulo < 0) modulo += 8;
    if (cell.waterDepth == 0 && modulo == 0) shade *= 0.82f;
    return MapScaleColor(base, MapClamp(shade, 0.62f, 1.24f));
}

const char *HomeWorldMapBiomeName(Biome biome, bool water)
{
    if (water) return "Water";
    switch (biome) {
    case BIOME_FOREST: return "Forest";
    case BIOME_DESERT: return "Desert";
    case BIOME_SNOW: return "Snow";
    case BIOME_MOUNTAIN: return "Mountain";
    case BIOME_PLAINS:
    default: return "Plains";
    }
}

float HomeWorldMapHeatSample(const float *heat, float u, float v)
{
    if (!heat) return 0.0f;
    u = MapClamp(u, 0.0f, 1.0f) * (float)(HOMEWORLD_MAP_HEAT_SIZE - 1);
    v = MapClamp(v, 0.0f, 1.0f) * (float)(HOMEWORLD_MAP_HEAT_SIZE - 1);
    int x0 = (int)floorf(u);
    int y0 = (int)floorf(v);
    int x1 = x0 + 1 < HOMEWORLD_MAP_HEAT_SIZE ? x0 + 1 : x0;
    int y1 = y0 + 1 < HOMEWORLD_MAP_HEAT_SIZE ? y0 + 1 : y0;
    float tx = u - (float)x0;
    float ty = v - (float)y0;
    float a = heat[y0 * HOMEWORLD_MAP_HEAT_SIZE + x0];
    float b = heat[y0 * HOMEWORLD_MAP_HEAT_SIZE + x1];
    float c = heat[y1 * HOMEWORLD_MAP_HEAT_SIZE + x0];
    float d = heat[y1 * HOMEWORLD_MAP_HEAT_SIZE + x1];
    return (a + (b - a) * tx) * (1.0f - ty) +
           (c + (d - c) * tx) * ty;
}

static bool MapLandmarkFarEnough(const HomeWorldMapLandmark *landmarks,
                                 int count, int x, int z, float minDistance)
{
    float minDistanceSq = minDistance * minDistance;
    for (int i = 0; i < count; i++) {
        float dx = (float)x - (float)landmarks[i].x;
        float dz = (float)z - (float)landmarks[i].z;
        if (dx * dx + dz * dz < minDistanceSq) return false;
    }
    return true;
}

static bool MapCellLocalMaximum(const HomeWorldMapTerrainCell *cells,
                                int x, int z, bool fauna)
{
    const HomeWorldMapTerrainCell *cell =
        &cells[z * HOMEWORLD_MAP_RASTER_SIZE + x];
    float value = fauna ? cell->faunaActivity : cell->elevation;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dz == 0) continue;
            const HomeWorldMapTerrainCell *neighbor =
                &cells[(z + dz) * HOMEWORLD_MAP_RASTER_SIZE + x + dx];
            float other = fauna ? neighbor->faunaActivity : neighbor->elevation;
            if (other > value) return false;
        }
    }
    return true;
}

static float MapLandmarkScore(const HomeWorldMapTerrainCell *cells,
                              int x, int z,
                              HomeWorldMapLandmarkKind kind)
{
    const HomeWorldMapTerrainCell *cell =
        &cells[z * HOMEWORLD_MAP_RASTER_SIZE + x];
    switch (kind) {
    case HOMEWORLD_MAP_LANDMARK_PEAK:
        if (cell->waterDepth > 0 ||
            cell->elevation < cell->seaLevel + 16.0f ||
            !MapCellLocalMaximum(cells, x, z, false)) return -1.0f;
        return 1000.0f + cell->elevation;
    case HOMEWORLD_MAP_LANDMARK_SHORE:
        if (cell->waterDepth > 0) return -1.0f;
        for (int dz = -1; dz <= 1; dz++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (cells[(z + dz) * HOMEWORLD_MAP_RASTER_SIZE + x + dx]
                        .waterDepth > 0) {
                    return 700.0f + cell->faunaActivity * 20.0f;
                }
            }
        }
        return -1.0f;
    case HOMEWORLD_MAP_LANDMARK_FOREST:
        if (cell->waterDepth > 0 ||
            (cell->planetSurface
                 ? cell->planetBiome != PLANET_BIOME_FOREST
                 : cell->biome != BIOME_FOREST)) return -1.0f;
        for (int dz = -1; dz <= 1; dz++) {
            for (int dx = -1; dx <= 1; dx++) {
                const HomeWorldMapTerrainCell *neighbor =
                    &cells[(z + dz) * HOMEWORLD_MAP_RASTER_SIZE + x + dx];
                if (neighbor->planetSurface
                        ? neighbor->planetBiome != PLANET_BIOME_FOREST
                        : neighbor->biome != BIOME_FOREST) return -1.0f;
            }
        }
        return 500.0f + cell->faunaActivity * 80.0f + cell->elevation * 0.02f;
    case HOMEWORLD_MAP_LANDMARK_FAUNA:
        if (cell->faunaActivity < 0.55f ||
            !MapCellLocalMaximum(cells, x, z, true)) return -1.0f;
        return 900.0f + cell->faunaActivity * 100.0f;
    default: return -1.0f;
    }
}

int HomeWorldMapSelectLandmarks(
    const HomeWorldMapTerrainCell *cells, HomeWorldMapBounds bounds,
    HomeWorldMapLandmark *out, int capacity)
{
    if (!cells || !out || capacity <= 0 || bounds.span <= 0.0f) return 0;
    if (capacity > HOMEWORLD_MAP_MAX_LANDMARKS) {
        capacity = HOMEWORLD_MAP_MAX_LANDMARKS;
    }

    int count = 0;
    float minDistance = bounds.span * 0.14f;
    for (int kind = HOMEWORLD_MAP_LANDMARK_PEAK;
         kind <= HOMEWORLD_MAP_LANDMARK_FAUNA && count < capacity; kind++) {
        for (int slot = 0; slot < 2 && count < capacity; slot++) {
            float bestScore = -1.0f;
            int bestX = -1;
            int bestZ = -1;
            for (int z = 1; z < HOMEWORLD_MAP_RASTER_SIZE - 1; z += 2) {
                for (int x = 1; x < HOMEWORLD_MAP_RASTER_SIZE - 1; x += 2) {
                    float score = MapLandmarkScore(
                        cells, x, z, (HomeWorldMapLandmarkKind)kind);
                    float worldX = bounds.centerX - bounds.span * 0.5f +
                        ((float)x + 0.5f) * bounds.span /
                            (float)HOMEWORLD_MAP_RASTER_SIZE;
                    float worldZ = bounds.centerZ - bounds.span * 0.5f +
                        ((float)z + 0.5f) * bounds.span /
                            (float)HOMEWORLD_MAP_RASTER_SIZE;
                    if (score > bestScore && MapLandmarkFarEnough(
                            out, count, (int)lroundf(worldX),
                            (int)lroundf(worldZ), minDistance)) {
                        bestScore = score;
                        bestX = x;
                        bestZ = z;
                    }
                }
            }
            if (bestX < 0) break;
            const HomeWorldMapTerrainCell *cell =
                &cells[bestZ * HOMEWORLD_MAP_RASTER_SIZE + bestX];
            out[count++] = (HomeWorldMapLandmark){
                .kind = (HomeWorldMapLandmarkKind)kind,
                .x = (int)lroundf(bounds.centerX - bounds.span * 0.5f +
                    ((float)bestX + 0.5f) * bounds.span /
                        (float)HOMEWORLD_MAP_RASTER_SIZE),
                .z = (int)lroundf(bounds.centerZ - bounds.span * 0.5f +
                    ((float)bestZ + 0.5f) * bounds.span /
                        (float)HOMEWORLD_MAP_RASTER_SIZE),
                .elevation = (int)lroundf(cell->elevation),
                .score = bestScore
            };
        }
    }
    return count;
}
