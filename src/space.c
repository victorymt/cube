#include "space.h"

#include "raymath.h"
#include "chunks.h"
#include "terrain.h"
#include "particles.h"
#include "world.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ASTEROID_SPACING 26
#define ASTEROID_PROBABILITY 55u
#define SPACE_MESH_REBUILDS_PER_FRAME 2

static const char *const starNamePart1[] = {
    "Al", "Bel", "Cer", "Dra", "Eri", "Fen", "Gar", "Hal", "Ith", "Jun",
    "Kel", "Lor", "Mir", "Neb", "Or", "Pry", "Quel", "Rav", "Tha", "Umb",
    "Vex", "Wy", "Zor", "Xan"
};
static const char *const starNamePart2[] = {
    "a", "e", "i", "o", "u", "ae", "ia", "or", "yn", "ei"
};
static const char *const starNamePart3[] = {
    "va", "nis", "dar", "lune", "rax", "thys", "mar", "dus", "phe", "rith"
};

static void BuildStarName(int ax, int az, char *out, size_t outSize)
{
    unsigned int h = Hash2D(ax * 31 + 7, az * 17 + 5);
    int p1 = (int)(h % 24u);
    int p2 = (int)((h >> 6) % 10u);
    int p3 = (int)((h >> 12) % 10u);
    snprintf(out, outSize, "%s%s%s", starNamePart1[p1], starNamePart2[p2], starNamePart3[p3]);
}

static void BuildSolSystem(SolarSystemDef *out)
{
    out->exists = true;
    out->anchorX = 0;
    out->anchorZ = 0;
    snprintf(out->name, sizeof(out->name), "Sol");
    out->spectrum = SPECTRUM_YELLOW;
    out->starRadius = 13;
    out->center = (Vector3){ 0.0f, (float)SPACE_LAYER_Y + 48.0f, 0.0f };
    out->planetCount = 6;
    static const SolarPlanetDef solPlanets[6] = {
        { 180, 6, -22, SOLAR_STYLE_LAVA },
        { 260, 5,  20, SOLAR_STYLE_ICE },
        { 340, 7,  -8, SOLAR_STYLE_DESERT },
        { 430, 4,  30, SOLAR_STYLE_GAS },
        { 520, 5, -28, SOLAR_STYLE_CRATER },
        { 650, 3,  14, SOLAR_STYLE_LAVA }
    };
    for (int i = 0; i < 6; i++) out->planets[i] = solPlanets[i];
}

bool StarSystemAt(int ax, int az, SolarSystemDef *out)
{
    if (ax == 0 && az == 0) {
        BuildSolSystem(out);
        return true;
    }

    out->exists = false;
    out->anchorX = ax;
    out->anchorZ = az;
    out->planetCount = 0;

    unsigned int roll = Hash2D(ax, az);
    if (roll % 100u >= STAR_SYSTEM_PROBABILITY) return false;

    unsigned int h = Hash2D(ax * 31 + 7, az * 17 + 5);
    out->exists = true;
    BuildStarName(ax, az, out->name, sizeof(out->name));
    out->spectrum = (SpectrumType)(h % 5u);
    out->starRadius = (out->spectrum == SPECTRUM_RED_GIANT) ? 10 + (int)(h % 6u) : 9 + (int)(h % 6u);
    out->center = (Vector3){
        (float)ax * (float)STAR_SYSTEM_SPACING,
        (float)SPACE_LAYER_Y + 40.0f + (float)(Hash2D(ax + 7, az + 13) % 40u),
        (float)az * (float)STAR_SYSTEM_SPACING
    };
    out->planetCount = 2 + (int)((h >> 8) % 4u);

    for (int i = 0; i < out->planetCount; i++) {
        unsigned int ph = Hash2D(ax * 53 + i * 7 + 1, az * 29 + i * 3 + 2);
        out->planets[i].orbit = 180 + i * 120 + (int)(ph % 5u) * 8;
        out->planets[i].size = 3 + (int)((ph >> 6) % 5u);
        out->planets[i].yOffset = (int)((ph >> 12) % 81u) - 40;
        out->planets[i].style = (SolarBodyStyle)(1 + ((ph >> 18) % 5u));
    }
    return true;
}

Vector3 SolarSystemPlanetCenter(const SolarSystemDef *sys, int index)
{
    const SolarPlanetDef *def = &sys->planets[index];
    float angle = (float)(Hash2D(sys->anchorX * 53 + index * 7 + 1,
                                 sys->anchorZ * 29 + index * 3 + 2) % 6283u) / 1000.0f;
    return (Vector3){
        sys->center.x + cosf(angle) * (float)def->orbit,
        sys->center.y + (float)def->yOffset,
        sys->center.z + sinf(angle) * (float)def->orbit
    };
}

Color SpectrumColor(SpectrumType type)
{
    switch (type) {
    case SPECTRUM_RED_DWARF: return (Color){ 255, 120, 90, 255 };
    case SPECTRUM_ORANGE:    return (Color){ 255, 170, 90, 255 };
    case SPECTRUM_YELLOW:    return (Color){ 255, 214, 120, 255 };
    case SPECTRUM_BLUE_WHITE: return (Color){ 190, 210, 255, 255 };
    case SPECTRUM_RED_GIANT: return (Color){ 255, 90, 60, 255 };
    default:                 return (Color){ 255, 214, 120, 255 };
    }
}

const char *SpectrumName(SpectrumType type)
{
    switch (type) {
    case SPECTRUM_RED_DWARF: return "Red Dwarf";
    case SPECTRUM_ORANGE:    return "Orange Star";
    case SPECTRUM_YELLOW:    return "Yellow Sun";
    case SPECTRUM_BLUE_WHITE: return "Blue-White Star";
    case SPECTRUM_RED_GIANT: return "Red Giant";
    default:                 return "Star";
    }
}

const char *SolarStyleName(SolarBodyStyle style)
{
    switch (style) {
    case SOLAR_STYLE_LAVA:   return "Lava Planet";
    case SOLAR_STYLE_ICE:    return "Ice Planet";
    case SOLAR_STYLE_DESERT: return "Desert Planet";
    case SOLAR_STYLE_GAS:    return "Gas Giant";
    case SOLAR_STYLE_CRATER: return "Cratered World";
    default:                 return "Planet";
    }
}

static BlockType StarBlock(int bx, int by, int bz, float distSqr, float shellSqr, SpectrumType spectrum)
{
    unsigned int h = Hash3D(bx, by, bz);
    bool surface = distSqr >= shellSqr;

    switch (spectrum) {
    case SPECTRUM_RED_DWARF:
        if (surface) {
            if (h % 5u == 0u) return BLOCK_LAVA;
            if (h % 9u == 0u) return BLOCK_GLOWSTONE;
            if (h % 11u == 0u) return BLOCK_METEORITE;
            return BLOCK_MOON_ROCK;
        }
        return (h % 7u == 0u) ? BLOCK_GLOWSTONE : BLOCK_MOON_ROCK;
    case SPECTRUM_ORANGE:
        if (surface) {
            if (h % 7u == 0u) return BLOCK_LAVA;
            if (h % 5u == 0u) return BLOCK_GLOWSTONE;
            return BLOCK_MOON_SAND;
        }
        return (h % 9u == 0u) ? BLOCK_STAR_MATTER : BLOCK_GLOWSTONE;
    case SPECTRUM_YELLOW:
        if (!surface) return (h % 5u == 0u) ? BLOCK_STAR_MATTER : BLOCK_GLOWSTONE;
        if (h % 9u == 0u) return BLOCK_LAVA;
        if (h % 4u == 0u) return BLOCK_STAR_MATTER;
        return BLOCK_GLOWSTONE;
    case SPECTRUM_BLUE_WHITE:
        if (surface) {
            if (h % 6u == 0u) return BLOCK_ICE;
            if (h % 9u == 0u) return BLOCK_MOON_SAND;
            if (h % 5u == 0u) return BLOCK_GLOWSTONE;
            return BLOCK_STAR_MATTER;
        }
        return (h % 7u == 0u) ? BLOCK_GLOWSTONE : BLOCK_STAR_MATTER;
    case SPECTRUM_RED_GIANT:
        if (surface) {
            if (h % 3u == 0u) return BLOCK_LAVA;
            if (h % 8u == 0u) return BLOCK_METEORITE;
            if (h % 7u == 0u) return BLOCK_GLOWSTONE;
            return BLOCK_MOON_ROCK;
        }
        return (h % 5u == 0u) ? BLOCK_LAVA : BLOCK_GLOWSTONE;
    default:
        return BLOCK_GLOWSTONE;
    }
}

static BlockType SolarBodyBlock(int bx, int by, int bz, float distSqr, float shellSqr, SolarBodyStyle style)
{
    unsigned int h = Hash3D(bx, by, bz);
    bool surface = distSqr >= shellSqr;

    switch (style) {
    case SOLAR_STYLE_LAVA:
        if (surface) return (h % 7u == 0u) ? BLOCK_LAVA : BLOCK_MOON_ROCK;
        return (h % 11u == 0u) ? BLOCK_METEORITE : BLOCK_MOON_ROCK;
    case SOLAR_STYLE_ICE:
        if (surface) return (h % 6u == 0u) ? BLOCK_SNOW : BLOCK_ICE;
        return (h % 13u == 0u) ? BLOCK_MOON_SAND : BLOCK_MOON_ROCK;
    case SOLAR_STYLE_DESERT:
        if (surface) return (h % 8u == 0u) ? BLOCK_SANDSTONE : BLOCK_SAND;
        return (h % 9u == 0u) ? BLOCK_METEORITE : BLOCK_MOON_ROCK;
    case SOLAR_STYLE_GAS:
        if ((by % 6u) < 2u) return (h % 5u == 0u) ? BLOCK_GLOWSTONE : BLOCK_SOUL_SAND;
        return (h % 7u == 0u) ? BLOCK_MOON_SAND : BLOCK_MOON_ROCK;
    case SOLAR_STYLE_CRATER:
        if (surface) return (h % 9u == 0u) ? BLOCK_METEORITE : BLOCK_MOON_SAND;
        return BLOCK_MOON_ROCK;
    default:
        return BLOCK_MOON_ROCK;
    }
}

static void FillSolarBody(SpaceChunk *chunk, int startX, int startZ,
                          int cx, int cy, int cz, int radius, SolarBodyStyle style)
{
    int chunkMinX = startX;
    int chunkMaxX = startX + CHUNK_SIZE - 1;
    int chunkMinZ = startZ;
    int chunkMaxZ = startZ + CHUNK_SIZE - 1;
    if (cx + radius < chunkMinX || cx - radius > chunkMaxX) return;
    if (cz + radius < chunkMinZ || cz - radius > chunkMaxZ) return;

    float radiusSqr = (float)(radius * radius);
    float shellSqr = (float)((radius - 1) * (radius - 1));

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int bx = startX + lx;
                int by = SPACE_LAYER_Y + ly;
                int bz = startZ + lz;
                float dx = (float)(bx - cx);
                float dy = (float)(by - cy);
                float dz = (float)(bz - cz);
                float distSqr = dx * dx + dy * dy + dz * dz;
                if (distSqr >= radiusSqr) continue;
                chunk->blocks[lx][ly][lz] = (unsigned short)SolarBodyBlock(bx, by, bz, distSqr, shellSqr, style);
            }
        }
    }
}

static void FillStarBody(SpaceChunk *chunk, int startX, int startZ,
                         int cx, int cy, int cz, int radius, SpectrumType spectrum)
{
    int chunkMinX = startX;
    int chunkMaxX = startX + CHUNK_SIZE - 1;
    int chunkMinZ = startZ;
    int chunkMaxZ = startZ + CHUNK_SIZE - 1;
    if (cx + radius < chunkMinX || cx - radius > chunkMaxX) return;
    if (cz + radius < chunkMinZ || cz - radius > chunkMaxZ) return;

    float radiusSqr = (float)(radius * radius);
    float shellSqr = (float)((radius - 1) * (radius - 1));

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int bx = startX + lx;
                int by = SPACE_LAYER_Y + ly;
                int bz = startZ + lz;
                float dx = (float)(bx - cx);
                float dy = (float)(by - cy);
                float dz = (float)(bz - cz);
                float distSqr = dx * dx + dy * dy + dz * dz;
                if (distSqr >= radiusSqr) continue;
                chunk->blocks[lx][ly][lz] = (unsigned short)StarBlock(bx, by, bz, distSqr, shellSqr, spectrum);
            }
        }
    }
}

static void FillSolarSystemsInChunk(SpaceChunk *chunk, int startX, int startZ)
{
    int minAnchorX = FloorDivInt(startX - 900, STAR_SYSTEM_SPACING);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 900, STAR_SYSTEM_SPACING);
    int minAnchorZ = FloorDivInt(startZ - 900, STAR_SYSTEM_SPACING);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 900, STAR_SYSTEM_SPACING);

    for (int ax = minAnchorX; ax <= maxAnchorX; ax++) {
        for (int az = minAnchorZ; az <= maxAnchorZ; az++) {
            SolarSystemDef sys;
            if (!StarSystemAt(ax, az, &sys)) continue;

            FillStarBody(chunk, startX, startZ,
                         (int)sys.center.x, (int)sys.center.y, (int)sys.center.z,
                         sys.starRadius, sys.spectrum);

            for (int i = 0; i < sys.planetCount; i++) {
                Vector3 center = SolarSystemPlanetCenter(&sys, i);
                FillSolarBody(chunk, startX, startZ,
                              (int)center.x, (int)center.y, (int)center.z,
                              sys.planets[i].size, sys.planets[i].style);
            }
        }
    }
}

int StarSystemsNear(Vector3 pos, float maxDist, SolarSystemDef *out, int maxCount)
{
    int count = 0;
    int centerAx = FloorDivInt((int)floorf(pos.x), STAR_SYSTEM_SPACING);
    int centerAz = FloorDivInt((int)floorf(pos.z), STAR_SYSTEM_SPACING);
    int radiusAnchors = (int)(maxDist / (float)STAR_SYSTEM_SPACING) + 1;

    SolarSystemDef found[256];
    float dists[256];
    int foundCount = 0;

    for (int ax = centerAx - radiusAnchors; ax <= centerAx + radiusAnchors; ax++) {
        for (int az = centerAz - radiusAnchors; az <= centerAz + radiusAnchors; az++) {
            SolarSystemDef sys;
            if (!StarSystemAt(ax, az, &sys)) continue;
            float dx = sys.center.x - pos.x;
            float dz = sys.center.z - pos.z;
            float d = sqrtf(dx * dx + dz * dz);
            if (d > maxDist) continue;
            if (foundCount < 256) {
                found[foundCount] = sys;
                dists[foundCount] = d;
                foundCount++;
            }
        }
    }

    for (int i = 0; i < foundCount && count < maxCount; i++) {
        int best = i;
        for (int j = i + 1; j < foundCount; j++) {
            if (dists[j] < dists[best]) best = j;
        }
        if (best != i) {
            SolarSystemDef tmpSys = found[i];
            found[i] = found[best];
            found[best] = tmpSys;
            float tmpDist = dists[i];
            dists[i] = dists[best];
            dists[best] = tmpDist;
        }
        out[count++] = found[i];
    }
    return count;
}

bool FindNearestSystem(Vector3 pos, float maxDist, SolarSystemDef *out, float *outDist)
{
    SolarSystemDef sys;
    int count = StarSystemsNear(pos, maxDist, &sys, 1);
    if (count < 1) return false;
    *out = sys;
    if (outDist) {
        float dx = sys.center.x - pos.x;
        float dz = sys.center.z - pos.z;
        *outDist = sqrtf(dx * dx + dz * dz);
    }
    return true;
}

int SpaceBodiesNear(Vector3 pos, float maxDist, SpaceBodyInfo *out, int maxCount)
{
    int count = 0;
    int centerAx = FloorDivInt((int)floorf(pos.x), STAR_SYSTEM_SPACING);
    int centerAz = FloorDivInt((int)floorf(pos.z), STAR_SYSTEM_SPACING);

    for (int ax = centerAx - 1; ax <= centerAx + 1; ax++) {
        for (int az = centerAz - 1; az <= centerAz + 1; az++) {
            SolarSystemDef sys;
            if (!StarSystemAt(ax, az, &sys)) continue;
            if (count >= maxCount) return count;

            Vector3 star = sys.center;
            float starDist = Vector3Distance(star, pos);
            if (starDist <= maxDist) {
                out[count] = (SpaceBodyInfo){
                    .center = star,
                    .radius = (float)sys.starRadius,
                    .dist = starDist,
                    .isStar = true,
                    .index = 0,
                    .spectrum = sys.spectrum
                };
                snprintf(out[count].name, sizeof(out[count].name), "%s", sys.name);
                count++;
            }

            for (int i = 0; i < sys.planetCount; i++) {
                if (count >= maxCount) return count;
                Vector3 center = SolarSystemPlanetCenter(&sys, i);
                float dist = Vector3Distance(center, pos);
                if (dist > maxDist) continue;
                out[count] = (SpaceBodyInfo){
                    .center = center,
                    .radius = (float)sys.planets[i].size,
                    .dist = dist,
                    .isStar = false,
                    .index = i + 1,
                    .style = sys.planets[i].style
                };
                snprintf(out[count].name, sizeof(out[count].name), "%s", sys.name);
                count++;
            }
        }
    }

    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (out[j].dist < out[i].dist) {
                SpaceBodyInfo tmp = out[i];
                out[i] = out[j];
                out[j] = tmp;
            }
        }
    }
    return count;
}

bool SpaceBodyPick(Vector3 origin, Vector3 direction, SpaceBodyInfo *out)
{
    SpaceBodyInfo bodies[48];
    int count = SpaceBodiesNear(origin, 700.0f, bodies, 48);
    float best = 1e30f;
    bool found = false;

    for (int i = 0; i < count; i++) {
        Vector3 to = Vector3Subtract(bodies[i].center, origin);
        float proj = Vector3DotProduct(to, direction);
        if (proj < 0.0f || proj > best) continue;
        Vector3 closest = Vector3Add(origin, Vector3Scale(direction, proj));
        Vector3 diff = Vector3Subtract(closest, bodies[i].center);
        float lateral = sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
        float radius = bodies[i].radius + 2.0f;
        if (lateral > radius) continue;
        best = proj;
        *out = bodies[i];
        found = true;
    }
    return found;
}

SpaceChunk spaceChunks[MAX_SPACE_CHUNKS];
static BlockEdit spaceEdits[MAX_SPACE_EDITS];
static int spaceEditCount = 0;

void SpaceInit(void)
{
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        spaceChunks[i].loaded = false;
        spaceChunks[i].dirty = false;
    }
    spaceEditCount = 0;
}

static SpaceChunk *FindSpaceChunk(int cx, int cz)
{
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (spaceChunks[i].loaded && spaceChunks[i].cx == cx && spaceChunks[i].cz == cz) return &spaceChunks[i];
    }
    return NULL;
}

static SpaceChunk *AllocateSpaceChunkSlot(int cx, int cz)
{
    SpaceChunk *empty = NULL;
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (!spaceChunks[i].loaded) {
            empty = &spaceChunks[i];
            break;
        }
    }
    if (!empty) return NULL;
    memset(empty, 0, sizeof(*empty));
    empty->cx = cx;
    empty->cz = cz;
    return empty;
}

static void UnloadSpaceChunkModel(SpaceChunk *chunk)
{
    if (chunk->hasModel) {
        UnloadModel(chunk->model);
        chunk->hasModel = false;
    }
    if (chunk->hasWaterModel) {
        UnloadModel(chunk->waterModel);
        chunk->hasWaterModel = false;
    }
}

static void ApplySpaceEditsToChunk(SpaceChunk *chunk)
{
    for (int i = 0; i < spaceEditCount; i++) {
        const BlockEdit *edit = &spaceEdits[i];
        if (edit->y < SPACE_LAYER_Y || edit->y >= SPACE_LAYER_TOP) continue;
        int editCx = 0;
        int editCz = 0;
        int editLx = 0;
        int editLz = 0;
        WorldToChunkLocal(edit->x, edit->z, &editCx, &editCz, &editLx, &editLz);
        if (editCx == chunk->cx && editCz == chunk->cz) {
            chunk->blocks[editLx][edit->y - SPACE_LAYER_Y][editLz] = (unsigned short)edit->type;
        }
    }
}


static void GenerateSpaceChunk(SpaceChunk *chunk, int cx, int cz)
{
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                chunk->blocks[lx][ly][lz] = (unsigned short)BLOCK_AIR;
            }
        }
    }

    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(startX - 8, ASTEROID_SPACING);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 8, ASTEROID_SPACING);
    int minAnchorZ = FloorDivInt(startZ - 8, ASTEROID_SPACING);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 8, ASTEROID_SPACING);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            if (Hash2D(anchorX, anchorZ) % 100u >= ASTEROID_PROBABILITY) continue;

            int wx = anchorX * ASTEROID_SPACING;
            int wz = anchorZ * ASTEROID_SPACING;
            int wy = SPACE_LAYER_Y + 8 + (int)(Hash2D(anchorX + 3, anchorZ) % (unsigned int)(WORLD_HEIGHT - 16));
            int radius = 3 + (int)(Hash2D(anchorX, anchorZ + 7) % 5u);
            float radiusSqr = (float)(radius * radius);
            float shellSqr = (float)((radius - 1) * (radius - 1));

            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
                    for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                        if (chunk->blocks[lx][ly][lz] != 0) continue;

                        int bx = startX + lx;
                        int by = SPACE_LAYER_Y + ly;
                        int bz = startZ + lz;
                        float dx = (float)(bx - wx);
                        float dy = (float)(by - wy);
                        float dz = (float)(bz - wz);
                        float distSqr = dx * dx + dy * dy + dz * dz;
                        if (distSqr >= radiusSqr) continue;

                        BlockType type = (distSqr >= shellSqr) ? BLOCK_MOON_SAND : BLOCK_MOON_ROCK;
                        if (Hash3D(bx, by, bz) % 89u == 0u) type = BLOCK_METEORITE;
                        chunk->blocks[lx][ly][lz] = (unsigned short)type;
                    }
                }
            }
        }
    }

    const int planetSpacing = 160;
    int minPlanetAnchorX = FloorDivInt(startX - 13, planetSpacing);
    int maxPlanetAnchorX = FloorDivInt(startX + CHUNK_SIZE + 13, planetSpacing);
    int minPlanetAnchorZ = FloorDivInt(startZ - 13, planetSpacing);
    int maxPlanetAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 13, planetSpacing);

    for (int anchorX = minPlanetAnchorX; anchorX <= maxPlanetAnchorX; anchorX++) {
        for (int anchorZ = minPlanetAnchorZ; anchorZ <= maxPlanetAnchorZ; anchorZ++) {
            if (Hash2D(anchorX + 71, anchorZ + 71) % 100u >= 20u) continue;

            int wx = anchorX * planetSpacing;
            int wz = anchorZ * planetSpacing;
            int wy = SPACE_LAYER_Y + 12 + (int)(Hash2D(anchorX + 31, anchorZ + 41) % (unsigned int)(WORLD_HEIGHT - 24));
            int radius = 8 + (int)(Hash2D(anchorX + 51, anchorZ + 61) % 5u);
            float radiusSqr = (float)(radius * radius);
            float shellSqr = (float)((radius - 2) * (radius - 2));

            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
                    for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                        int bx = startX + lx;
                        int by = SPACE_LAYER_Y + ly;
                        int bz = startZ + lz;
                        float dx = (float)(bx - wx);
                        float dy = (float)(by - wy);
                        float dz = (float)(bz - wz);
                        float distSqr = dx * dx + dy * dy + dz * dz;
                        if (distSqr >= radiusSqr) continue;

                        BlockType type = (distSqr >= shellSqr) ? BLOCK_MOON_SAND : BLOCK_MOON_ROCK;
                        if (distSqr >= shellSqr && Hash3D(bx, by, bz) % 19u == 0u) type = BLOCK_MOON_ROCK;
                        if (Hash3D(bx, by, bz) % 41u == 0u) type = BLOCK_METEORITE;
                        chunk->blocks[lx][ly][lz] = (unsigned short)type;
                    }
                }
            }
        }
    }

    chunk->hasStar = false;
    const int starSpacing = 52;
    int minStarAnchorX = FloorDivInt(startX - 5, starSpacing);
    int maxStarAnchorX = FloorDivInt(startX + CHUNK_SIZE + 5, starSpacing);
    int minStarAnchorZ = FloorDivInt(startZ - 5, starSpacing);
    int maxStarAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 5, starSpacing);

    for (int anchorX = minStarAnchorX; anchorX <= maxStarAnchorX; anchorX++) {
        for (int anchorZ = minStarAnchorZ; anchorZ <= maxStarAnchorZ; anchorZ++) {
            if (Hash2D(anchorX + 101, anchorZ + 101) % 100u >= 35u) continue;

            int wx = anchorX * starSpacing;
            int wz = anchorZ * starSpacing;
            int wy = SPACE_LAYER_Y + 10 + (int)(Hash2D(anchorX + 5, anchorZ + 9) % (unsigned int)(WORLD_HEIGHT - 20));
            int radius = 2 + (int)(Hash2D(anchorX + 11, anchorZ + 13) % 3u);
            float radiusSqr = (float)(radius * radius);

            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
                    for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                        int bx = startX + lx;
                        int by = SPACE_LAYER_Y + ly;
                        int bz = startZ + lz;
                        float dx = (float)(bx - wx);
                        float dy = (float)(by - wy);
                        float dz = (float)(bz - wz);
                        if (dx * dx + dy * dy + dz * dz < radiusSqr) {
                            chunk->blocks[lx][ly][lz] = (unsigned short)BLOCK_STAR_MATTER;
                        }
                    }
                }
            }

            if (wx >= startX && wx < startX + CHUNK_SIZE &&
                wz >= startZ && wz < startZ + CHUNK_SIZE) {
                chunk->hasStar = true;
                chunk->starX = wx;
                chunk->starY = wy;
                chunk->starZ = wz;
            }
        }
    }

    FillSolarSystemsInChunk(chunk, startX, startZ);

    ApplySpaceEditsToChunk(chunk);
    chunk->loaded = true;
    chunk->dirty = true;
}

static void SpaceRememberEdit(int x, int y, int z, BlockType type)
{
    for (int i = 0; i < spaceEditCount; i++) {
        if (spaceEdits[i].x == x && spaceEdits[i].y == y && spaceEdits[i].z == z) {
            spaceEdits[i].type = type;
            return;
        }
    }
    if (spaceEditCount < MAX_SPACE_EDITS) {
        spaceEdits[spaceEditCount++] = (BlockEdit){ x, y, z, type };
    }
}

static void RebuildSpaceChunkMesh(SpaceChunk *chunk)
{
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };

    int nearbyTorchIndices[MAX_TORCH_LIGHTS];
    int nearbyTorchCount = CollectNearbyTorchLights(
        chunk->cx * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cx * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        nearbyTorchIndices);

    UnloadSpaceChunkModel(chunk);

    Mesh solidMesh = { 0 };
    Mesh waterMesh = { 0 };
    bool hasSolid = BuildMeshData((const unsigned short (*)[CHUNK_SIZE])chunk->blocks,
                                  SPACE_LAYER_HEIGHT, SPACE_LAYER_Y,
                                  chunk->cx, chunk->cz, false, faces,
                                  nearbyTorchIndices, nearbyTorchCount, &solidMesh);
    bool hasWater = BuildMeshData((const unsigned short (*)[CHUNK_SIZE])chunk->blocks,
                                  SPACE_LAYER_HEIGHT, SPACE_LAYER_Y,
                                  chunk->cx, chunk->cz, true, faces,
                                  nearbyTorchIndices, nearbyTorchCount, &waterMesh);

    if (hasSolid) {
        UploadMesh(&solidMesh, false);
        chunk->model = LoadModelFromMesh(solidMesh);
        SetMaterialTexture(&chunk->model.materials[0], MATERIAL_MAP_DIFFUSE, blockAtlas);
        chunk->hasModel = true;
    }
    if (hasWater) {
        UploadMesh(&waterMesh, false);
        chunk->waterModel = LoadModelFromMesh(waterMesh);
        SetMaterialTexture(&chunk->waterModel.materials[0], MATERIAL_MAP_DIFFUSE, blockAtlas);
        chunk->hasWaterModel = true;
    }
    chunk->dirty = false;
}

void UpdateSpaceChunks(Vector3 playerPosition, int groundRenderDistance, int generationPerFrame)
{
    int renderDist = SPACE_RENDER_DISTANCE_CHUNKS;
    if (groundRenderDistance < renderDist) renderDist = groundRenderDistance;

    int playerCx = 0;
    int playerCz = 0;
    int playerLx = 0;
    int playerLz = 0;
    WorldToChunkLocal((int)floorf(playerPosition.x), (int)floorf(playerPosition.z),
                      &playerCx, &playerCz, &playerLx, &playerLz);

    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (!spaceChunks[i].loaded) continue;
        if (abs(spaceChunks[i].cx - playerCx) > renderDist ||
            abs(spaceChunks[i].cz - playerCz) > renderDist) {
            UnloadSpaceChunkModel(&spaceChunks[i]);
            spaceChunks[i].loaded = false;
            spaceChunks[i].dirty = false;
        }
    }

    if (playerPosition.y < 50.0f) return;

    int generated = 0;
    for (int dz = -renderDist; dz <= renderDist && generated < generationPerFrame; dz++) {
        for (int dx = -renderDist; dx <= renderDist && generated < generationPerFrame; dx++) {
            int cx = playerCx + dx;
            int cz = playerCz + dz;
            if (FindSpaceChunk(cx, cz)) continue;
            SpaceChunk *chunk = AllocateSpaceChunkSlot(cx, cz);
            if (!chunk) break;
            GenerateSpaceChunk(chunk, cx, cz);
            generated++;
        }
    }

    int rebuilt = 0;
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (!spaceChunks[i].loaded || !spaceChunks[i].dirty) continue;
        RebuildSpaceChunkMesh(&spaceChunks[i]);
        if (++rebuilt >= SPACE_MESH_REBUILDS_PER_FRAME) break;
    }
}

BlockType SpaceBlockAt(int x, int y, int z)
{
    if (y < SPACE_LAYER_Y || y >= SPACE_LAYER_TOP) return BLOCK_AIR;

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    SpaceChunk *chunk = FindSpaceChunk(cx, cz);
    if (!chunk) return BLOCK_AIR;
    return (BlockType)chunk->blocks[lx][y - SPACE_LAYER_Y][lz];
}

void SpaceSetBlock(int x, int y, int z, BlockType type)
{
    if (y < SPACE_LAYER_Y || y >= SPACE_LAYER_TOP) return;

    SpaceRememberEdit(x, y, z, type);

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    SpaceChunk *chunk = FindSpaceChunk(cx, cz);
    if (!chunk) return;
    chunk->blocks[lx][y - SPACE_LAYER_Y][lz] = (unsigned short)type;
    chunk->dirty = true;
}

void SpaceSaveEdits(FILE *file)
{
    uint32_t count = (uint32_t)spaceEditCount;
    fwrite(&count, sizeof(count), 1, file);
    if (spaceEditCount > 0) {
        fwrite(spaceEdits, sizeof(BlockEdit), (size_t)spaceEditCount, file);
    }
}

void SpaceLoadEdits(FILE *file)
{
    spaceEditCount = 0;

    uint32_t count = 0;
    if (fread(&count, sizeof(count), 1, file) != 1) return;

    if (count > MAX_SPACE_EDITS) return;

    for (uint32_t i = 0; i < count; i++) {
        BlockEdit edit;
        if (fread(&edit, sizeof(edit), 1, file) != 1) break;
        if (edit.y < SPACE_LAYER_Y || edit.y >= SPACE_LAYER_TOP) continue;
        if (!IsValidBlockType(edit.type)) continue;
        if (spaceEditCount < MAX_SPACE_EDITS) spaceEdits[spaceEditCount++] = edit;
    }
}

void UnloadAllSpaceChunks(void)
{
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        UnloadSpaceChunkModel(&spaceChunks[i]);
        spaceChunks[i].loaded = false;
        spaceChunks[i].dirty = false;
    }
}

int GetActiveSpaceChunkCount(void)
{
    int count = 0;
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (spaceChunks[i].loaded) count++;
    }
    return count;
}

void SpaceRebuildTorchList(void)
{
    for (int i = 0; i < spaceEditCount; i++) {
        if (spaceEdits[i].type == BLOCK_TORCH) {
            TorchLightAdd(spaceEdits[i].x, spaceEdits[i].y, spaceEdits[i].z);
        }
    }
}

void SpaceUpdateStarGlow(Vector3 playerPosition)
{
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        SpaceChunk *chunk = &spaceChunks[i];
        if (!chunk->loaded || !chunk->hasStar) continue;

        if (SpaceBlockAt(chunk->starX, chunk->starY, chunk->starZ) != BLOCK_STAR_MATTER) continue;

        Vector3 star = { (float)chunk->starX + 0.5f, (float)chunk->starY + 0.5f, (float)chunk->starZ + 0.5f };
        float dist = Vector3Distance(star, playerPosition);
        if (dist > 18.0f) continue;

        int count = (dist < 9.0f) ? 2 : 1;
        for (int k = 0; k < count; k++) {
            Vector3 offset = {
                ((float)rand() / (float)RAND_MAX - 0.5f) * 3.0f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 3.0f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 3.0f
            };
            ParticlesEmitOne(Vector3Add(star, offset),
                             (Vector3){ 0.1f, 0.35f, 0.1f },
                             (Color){ 255, 244, 190, 220 },
                             (Vector3){ 0.09f, 0.09f, 0.09f },
                             1.4f, 0.0f);
        }
    }
}

int GetSpaceEditCount(void)
{
    return spaceEditCount;
}

void SpaceUpdateSolarGlow(Vector3 playerPosition)
{
    SolarSystemDef sys;
    float sysDist = 0.0f;
    if (!FindNearestSystem(playerPosition, 60.0f, &sys, &sysDist)) return;

    float dist = Vector3Distance(sys.center, playerPosition);
    int count = (dist < 24.0f) ? 3 : 1;
    Color glow = SpectrumColor(sys.spectrum);
    for (int k = 0; k < count; k++) {
        Vector3 offset = {
            ((float)rand() / (float)RAND_MAX - 0.5f) * 10.0f,
            ((float)rand() / (float)RAND_MAX - 0.5f) * 10.0f,
            ((float)rand() / (float)RAND_MAX - 0.5f) * 10.0f
        };
        ParticlesEmitOne(Vector3Add(sys.center, offset),
                         (Vector3){ ((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f,
                                    0.2f + (float)rand() / (float)RAND_MAX * 0.5f,
                                    ((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f },
                         glow,
                         (Vector3){ 0.14f, 0.14f, 0.14f },
                         1.8f, 0.0f);
    }
}
