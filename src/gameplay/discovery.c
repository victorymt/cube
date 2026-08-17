#include "gameplay/discovery.h"

#include "core/game_notice.h"
#include "world/chunks.h"
#include "gameplay/inventory.h"
#include "space/space_state.h"
#include "world/terrain.h"
#include "world/world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLANET_POI_REGION_SIZE 192
#define PLANET_POI_MARGIN 20
#define PLANET_POI_STRUCTURE_RADIUS 3

static uint32_t PlanetPoiMix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static uint32_t PlanetPoiHash(int cellX, int cellZ, uint32_t salt)
{
    uint32_t value = PlanetWorldSeed() ^ salt;
    value ^= (uint32_t)cellX * 0x9e3779b9u;
    value ^= (uint32_t)cellZ * 0x85ebca6bu;
    return PlanetPoiMix(value);
}

static int PlanetPoiFloorDiv(int value, int divisor)
{
    int quotient = value / divisor;
    int remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

static bool PlanetPoiIsWaterBiome(PlanetBiome biome)
{
    return biome == PLANET_BIOME_OCEAN || biome == PLANET_BIOME_LAVA_SEA ||
           biome == PLANET_BIOME_STORM_BANDS;
}

static PlanetPoiType PlanetPoiTypeForBiome(PlanetBiome biome, uint32_t variation)
{
    if (biome == PLANET_BIOME_BASALT_PLAINS || biome == PLANET_BIOME_VOLCANIC_RIDGE ||
        biome == PLANET_BIOME_GLACIER || biome == PLANET_BIOME_ICE_SHEET) {
        return variation % 5u == 0u ? PLANET_POI_ANOMALY : PLANET_POI_RESOURCE_CACHE;
    }
    if (biome == PLANET_BIOME_DUNES || biome == PLANET_BIOME_BADLANDS ||
        biome == PLANET_BIOME_IMPACT_BASIN || biome == PLANET_BIOME_CRATER_HIGHLANDS) {
        return variation % 5u == 0u ? PLANET_POI_RESOURCE_CACHE : PLANET_POI_RELIC;
    }
    return variation % 4u == 0u ? PLANET_POI_RELIC : PLANET_POI_ANOMALY;
}

static void PlanetPoiConfigure(PlanetPoi *poi, PlanetPoiType type)
{
    poi->type = type;
    switch (type) {
    case PLANET_POI_RELIC:
        poi->coreBlock = BLOCK_GLOWSTONE;
        poi->rewardBlock = BLOCK_GOLD_ORE;
        poi->rewardAmount = 3;
        snprintf(poi->name, sizeof(poi->name), "Ancient relic");
        break;
    case PLANET_POI_RESOURCE_CACHE:
        poi->coreBlock = BLOCK_DIAMOND_ORE;
        poi->rewardBlock = BLOCK_DIAMOND_ORE;
        poi->rewardAmount = 2;
        snprintf(poi->name, sizeof(poi->name), "Resource cache");
        break;
    case PLANET_POI_ANOMALY:
    default:
        poi->coreBlock = BLOCK_STAR_MATTER;
        poi->rewardBlock = BLOCK_STAR_MATTER;
        poi->rewardAmount = 1;
        snprintf(poi->name, sizeof(poi->name), "Signal anomaly");
        break;
    }
}

static bool PlanetPoiForCell(int cellX, int cellZ, PlanetPoi *out)
{
    if (!PlanetWorldIsActive()) return false;

    int baseX = cellX * PLANET_POI_REGION_SIZE;
    int baseZ = cellZ * PLANET_POI_REGION_SIZE;
    int span = PLANET_POI_REGION_SIZE - PLANET_POI_MARGIN * 2;
    for (int attempt = 0; attempt < 8; attempt++) {
        uint32_t xHash = PlanetPoiHash(cellX, cellZ, 0x31d5u + (uint32_t)attempt * 17u);
        uint32_t zHash = PlanetPoiHash(cellX, cellZ, 0x9a41u + (uint32_t)attempt * 23u);
        int x = baseX + PLANET_POI_MARGIN + (int)(xHash % (uint32_t)span);
        int z = baseZ + PLANET_POI_MARGIN + (int)(zHash % (uint32_t)span);
        int groundY = PlanetTerrainHeight(x, z);
        PlanetBiome biome = PlanetBiomeAt(x, z);
        if (PlanetPoiIsWaterBiome(biome) ||
            groundY > SURFACE_GENERATION_MAX_Y_EXCLUSIVE - 6) continue;

        out->x = x;
        out->y = groundY + 2;
        out->z = z;
        PlanetPoiConfigure(out, PlanetPoiTypeForBiome(biome, xHash ^ zHash));
        return true;
    }
    return false;
}

static bool PlanetPoiAtCore(int x, int y, int z, PlanetPoi *out)
{
    PlanetPoi poi = { 0 };
    int cellX = PlanetPoiFloorDiv(x, PLANET_POI_REGION_SIZE);
    int cellZ = PlanetPoiFloorDiv(z, PLANET_POI_REGION_SIZE);
    if (!PlanetPoiForCell(cellX, cellZ, &poi)) return false;
    if (poi.x != x || poi.y != y || poi.z != z) return false;
    if (out) *out = poi;
    return true;
}

bool PlanetPoiIsCore(int x, int y, int z)
{
    PlanetPoi poi = { 0 };
    return PlanetPoiAtCore(x, y, z, &poi) && GetBlockAt(x, y, z) == poi.coreBlock;
}

bool PlanetPoiIsClaimed(int x, int y, int z)
{
    return PlanetPoiAtCore(x, y, z, NULL) && GetBlockAt(x, y, z) == BLOCK_BEDROCK;
}

static void PlanetPoiSet(Chunk *chunk, int x, int y, int z, BlockType type)
{
    if (InHeight(y)) SetChunkLocalBlock(chunk, x, y, z, type);
}

static void PlanetPoiClearVolume(Chunk *chunk, const PlanetPoi *poi, int radius)
{
    for (int x = poi->x - radius; x <= poi->x + radius; x++) {
        for (int z = poi->z - radius; z <= poi->z + radius; z++) {
            for (int y = poi->y - 1; y <= poi->y + 3; y++) {
                PlanetPoiSet(chunk, x, y, z, BLOCK_AIR);
            }
        }
    }
}

static void PlanetPoiPlaceRelic(Chunk *chunk, const PlanetPoi *poi)
{
    PlanetPoiClearVolume(chunk, poi, 2);
    for (int x = poi->x - 2; x <= poi->x + 2; x++) {
        for (int z = poi->z - 2; z <= poi->z + 2; z++) {
            PlanetPoiSet(chunk, x, poi->y - 2, z,
                         (abs(x - poi->x) + abs(z - poi->z)) % 3 == 0 ?
                         BLOCK_SANDSTONE : BLOCK_MOON_ROCK);
        }
    }
    for (int sx = -2; sx <= 2; sx += 4) {
        for (int sz = -2; sz <= 2; sz += 4) {
            for (int y = poi->y - 1; y <= poi->y + 2; y++) {
                PlanetPoiSet(chunk, poi->x + sx, y, poi->z + sz, BLOCK_MOON_ROCK);
            }
        }
    }
    PlanetPoiSet(chunk, poi->x, poi->y, poi->z, poi->coreBlock);
}

static void PlanetPoiPlaceResourceCache(Chunk *chunk, const PlanetPoi *poi)
{
    PlanetPoiClearVolume(chunk, poi, 1);
    for (int x = poi->x - 1; x <= poi->x + 1; x++) {
        for (int z = poi->z - 1; z <= poi->z + 1; z++) {
            PlanetPoiSet(chunk, x, poi->y - 2, z, BLOCK_METEORITE);
        }
    }
    PlanetPoiSet(chunk, poi->x - 1, poi->y - 1, poi->z, BLOCK_IRON_ORE);
    PlanetPoiSet(chunk, poi->x + 1, poi->y - 1, poi->z, BLOCK_GOLD_ORE);
    PlanetPoiSet(chunk, poi->x, poi->y - 1, poi->z - 1, BLOCK_IRON_ORE);
    PlanetPoiSet(chunk, poi->x, poi->y - 1, poi->z + 1, BLOCK_GOLD_ORE);
    PlanetPoiSet(chunk, poi->x, poi->y, poi->z, poi->coreBlock);
}

static void PlanetPoiPlaceAnomaly(Chunk *chunk, const PlanetPoi *poi)
{
    PlanetPoiClearVolume(chunk, poi, 2);
    for (int x = poi->x - 2; x <= poi->x + 2; x++) {
        for (int z = poi->z - 2; z <= poi->z + 2; z++) {
            int distance = abs(x - poi->x) + abs(z - poi->z);
            if (distance == 3 || distance == 4) {
                PlanetPoiSet(chunk, x, poi->y - 1, z, BLOCK_OBSIDIAN);
            }
        }
    }
    PlanetPoiSet(chunk, poi->x - 1, poi->y, poi->z, BLOCK_GLOWSTONE);
    PlanetPoiSet(chunk, poi->x + 1, poi->y, poi->z, BLOCK_GLOWSTONE);
    PlanetPoiSet(chunk, poi->x, poi->y, poi->z - 1, BLOCK_GLOWSTONE);
    PlanetPoiSet(chunk, poi->x, poi->y, poi->z + 1, BLOCK_GLOWSTONE);
    PlanetPoiSet(chunk, poi->x, poi->y, poi->z, poi->coreBlock);
}

static void PlanetPoiPlace(Chunk *chunk, const PlanetPoi *poi)
{
    switch (poi->type) {
    case PLANET_POI_RELIC:
        PlanetPoiPlaceRelic(chunk, poi);
        break;
    case PLANET_POI_RESOURCE_CACHE:
        PlanetPoiPlaceResourceCache(chunk, poi);
        break;
    case PLANET_POI_ANOMALY:
    default:
        PlanetPoiPlaceAnomaly(chunk, poi);
        break;
    }
}

void PlanetPoiApplyToChunk(Chunk *chunk, int chunkX, int chunkZ)
{
    if (!PlanetWorldIsActive()) return;

    int minX = chunkX * CHUNK_SIZE - PLANET_POI_STRUCTURE_RADIUS;
    int minZ = chunkZ * CHUNK_SIZE - PLANET_POI_STRUCTURE_RADIUS;
    int maxX = chunkX * CHUNK_SIZE + CHUNK_SIZE - 1 + PLANET_POI_STRUCTURE_RADIUS;
    int maxZ = chunkZ * CHUNK_SIZE + CHUNK_SIZE - 1 + PLANET_POI_STRUCTURE_RADIUS;
    int firstCellX = PlanetPoiFloorDiv(minX, PLANET_POI_REGION_SIZE);
    int lastCellX = PlanetPoiFloorDiv(maxX, PLANET_POI_REGION_SIZE);
    int firstCellZ = PlanetPoiFloorDiv(minZ, PLANET_POI_REGION_SIZE);
    int lastCellZ = PlanetPoiFloorDiv(maxZ, PLANET_POI_REGION_SIZE);

    for (int cellX = firstCellX; cellX <= lastCellX; cellX++) {
        for (int cellZ = firstCellZ; cellZ <= lastCellZ; cellZ++) {
            PlanetPoi poi = { 0 };
            if (PlanetPoiForCell(cellX, cellZ, &poi)) PlanetPoiPlace(chunk, &poi);
        }
    }
}

bool PlanetPoiNearest(Vector3 playerPosition, PlanetPoi *out)
{
    if (!PlanetWorldIsActive()) return false;

    int playerCellX = PlanetPoiFloorDiv((int)floorf(playerPosition.x), PLANET_POI_REGION_SIZE);
    int playerCellZ = PlanetPoiFloorDiv((int)floorf(playerPosition.z), PLANET_POI_REGION_SIZE);
    float bestDistanceSq = INFINITY;
    PlanetPoi nearest = { 0 };
    bool found = false;
    for (int cellX = playerCellX - 2; cellX <= playerCellX + 2; cellX++) {
        for (int cellZ = playerCellZ - 2; cellZ <= playerCellZ + 2; cellZ++) {
            PlanetPoi poi = { 0 };
            if (!PlanetPoiForCell(cellX, cellZ, &poi)) continue;
            if (PlanetPoiIsClaimed(poi.x, poi.y, poi.z)) continue;

            float dx = playerPosition.x - ((float)poi.x + 0.5f);
            float dy = playerPosition.y - ((float)poi.y + 0.5f);
            float dz = playerPosition.z - ((float)poi.z + 0.5f);
            float distanceSq = dx * dx + dy * dy + dz * dz;
            if (distanceSq < bestDistanceSq) {
                bestDistanceSq = distanceSq;
                nearest = poi;
                found = true;
            }
        }
    }
    if (found && out) *out = nearest;
    return found;
}

bool PlanetPoiTryClaim(int x, int y, int z, PlanetPoi *out)
{
    PlanetPoi poi = { 0 };
    if (!PlanetPoiAtCore(x, y, z, &poi)) return false;
    if (GetBlockAt(x, y, z) != poi.coreBlock) return false;
    int added = InventoryAdd(poi.rewardBlock, poi.rewardAmount);
    if (added <= 0) {
        GameNoticePost("Inventory full: exploration reward not claimed.");
        return false;
    }

    // This marker is persisted in the existing block-edit save data and prevents
    // a scanned site from returning after its core has been collected.
    SetBlock(x, y, z, BLOCK_BEDROCK);
    if (out) {
        *out = poi;
        out->rewardAmount = added;
    }
    return true;
}
