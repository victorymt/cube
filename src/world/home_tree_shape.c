#include "world/home_tree_shape.h"

#include "world/chunks.h"
#include "world/surface_topology.h"
#include "world/world.h"

#include <math.h>
#include <stddef.h>

static uint32_t ShapeSeedMix(uint32_t hash)
{
    uint32_t seed = WorldGetSeed();
    if (seed == DEFAULT_WORLD_SEED) return hash;
    hash ^= seed + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    hash ^= hash >> 16;
    hash *= 2246822519u;
    return hash ^ (hash >> 13);
}

static uint32_t ShapeWorldHash3D(int x, int y, int z)
{
    SurfaceMapCell cell = SurfaceCanonicalMapCell((float)x, (float)z);
    return ShapeSeedMix(Hash3D(cell.x, y, cell.z));
}

static void TreeDirection(int direction, int *outX, int *outZ)
{
    static const int directions[4][2] = {
        { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }
    };
    int index = direction & 3;
    *outX = directions[index][0];
    *outZ = directions[index][1];
}

static bool EmitBlock(const HomeTreeShapeSpec *spec,
                      HomeTreeShapeEmitter emitter, void *context,
                      int x, int y, int z, BlockType block, bool replace)
{
    (void)spec;
    if (!InHeight(y)) return true;
    return emitter(context, x, y, z, block, replace);
}

static bool EmitLeafCluster(const HomeTreeShapeSpec *spec,
                            HomeTreeShapeEmitter emitter, void *context,
                            int centerX, int centerY, int centerZ,
                            int radiusX, int radiusY, int radiusZ,
                            uint32_t lane)
{
    bool compact = radiusX == 1 && radiusY == 1 && radiusZ == 1;
    float distanceLimit = compact ? 2.05f : 1.18f;
    for (int dx = -radiusX; dx <= radiusX; dx++) {
        for (int dy = -radiusY; dy <= radiusY; dy++) {
            for (int dz = -radiusZ; dz <= radiusZ; dz++) {
                float nx = (float)dx / (float)radiusX;
                float ny = (float)dy / (float)radiusY;
                float nz = (float)dz / (float)radiusZ;
                float distance = nx * nx + ny * ny + nz * nz;
                if (distance > distanceLimit) continue;
                uint32_t edgeHash = ShapeWorldHash3D(
                    centerX + dx, centerY + dy + (int)(lane % 503u),
                    centerZ + dz);
                if (distance > 0.72f && edgeHash % 6u == 0u) continue;
                if (!EmitBlock(spec, emitter, context,
                               centerX + dx, centerY + dy, centerZ + dz,
                               spec->accentBlock, false)) return false;
            }
        }
    }
    return true;
}

static bool EmitBranch(const HomeTreeShapeSpec *spec,
                       HomeTreeShapeEmitter emitter, void *context,
                       int treeX, int startY, int treeZ, int direction,
                       int reach, int rise, int bend,
                       int *outX, int *outY, int *outZ)
{
    int directionX = 0;
    int directionZ = 0;
    TreeDirection(direction, &directionX, &directionZ);
    int endX = treeX;
    int endY = startY;
    int endZ = treeZ;
    for (int step = 1; step <= reach; step++) {
        endX = treeX + directionX * step;
        endY = startY + (rise * step) / reach;
        endZ = treeZ + directionZ * step;
        if (!EmitBlock(spec, emitter, context, endX, endY, endZ,
                       spec->primaryBlock, true)) return false;
    }
    if (bend != 0) {
        endX += -directionZ * bend;
        endZ += directionX * bend;
        if (!EmitBlock(spec, emitter, context, endX, endY, endZ,
                       spec->primaryBlock, true)) return false;
    }
    if (outX) *outX = endX;
    if (outY) *outY = endY;
    if (outZ) *outZ = endZ;
    return true;
}

static bool EmitBroadleaf(const HomeTreeShapeSpec *spec,
                          HomeTreeShapeEmitter emitter, void *context)
{
    uint32_t shapeHash = spec->shapeHash;
    bool willow = spec->taxonId == FLORA_TAXON_WILLOW;
    bool birch = spec->taxonId == FLORA_TAXON_BIRCH;
    bool aspen = spec->taxonId == FLORA_TAXON_ASPEN;
    int trunkHeight = 8 + (int)(shapeHash % 3u);
    int branchCount = 5 + (int)((shapeHash >> 5) % 2u);
    int baseReach = 2;
    int reachRange = 1;
    int leafRadius = 2;
    if (willow) {
        trunkHeight = 6 + (int)(shapeHash % 2u);
        branchCount = 6 + (int)((shapeHash >> 5) % 2u);
        baseReach = 2;
        reachRange = 2;
        leafRadius = 1;
    } else if (birch) {
        trunkHeight = 10 + (int)(shapeHash % 3u);
        branchCount = 5;
        baseReach = 1;
        reachRange = 2;
        leafRadius = 1;
    } else if (aspen) {
        trunkHeight = 12 + (int)(shapeHash % 4u);
        branchCount = 6;
        baseReach = 1;
        reachRange = 2;
        leafRadius = 1;
    }

    for (int y = spec->baseY; y < spec->baseY + trunkHeight; y++) {
        if (!EmitBlock(spec, emitter, context, spec->rootX, y, spec->rootZ,
                       spec->primaryBlock, true)) return false;
    }

    int trunkTop = spec->baseY + trunkHeight - 1;
    int baseDirection = (int)((shapeHash >> 9) & 3u);
    for (int branch = 0; branch < branchCount; branch++) {
        uint32_t branchHash = ShapeWorldHash3D(
            spec->rootX, 601 + branch * 17, spec->rootZ);
        int startDepth = birch || aspen ? 5 : 3;
        int startY = trunkTop - startDepth +
                     (int)(branchHash % (unsigned int)startDepth);
        if (startY < spec->baseY + 2) startY = spec->baseY + 2;
        int reach = baseReach +
                    (int)((branchHash >> 3) % (unsigned int)reachRange);
        int rise = willow ? (int)((branchHash >> 6) % 2u) :
            1 + (int)((branchHash >> 6) % 2u);
        if (birch || aspen) {
            rise = 1 + (int)((branchHash >> 6) % 3u);
        }
        int endX = spec->rootX;
        int endY = startY;
        int endZ = spec->rootZ;
        if (!EmitBranch(spec, emitter, context, spec->rootX, startY,
                        spec->rootZ, baseDirection + branch, reach, rise, 0,
                        &endX, &endY, &endZ)) return false;
        int leafHeight = birch || aspen ? 2 : 1;
        if (!EmitLeafCluster(spec, emitter, context, endX, endY, endZ,
                             leafRadius, leafHeight, leafRadius,
                             branchHash)) return false;
        if (willow && !EmitLeafCluster(
                spec, emitter, context, endX, endY - 1, endZ,
                1, 2, 1, branchHash >> 3)) return false;
    }

    if (spec->taxonId == FLORA_TAXON_OAK) {
        return EmitLeafCluster(spec, emitter, context,
                               spec->rootX, trunkTop + 1, spec->rootZ,
                               2, 2, 2, shapeHash);
    }
    if (willow) {
        return EmitLeafCluster(spec, emitter, context,
                               spec->rootX, trunkTop + 1, spec->rootZ,
                               3, 1, 3, shapeHash) &&
               EmitLeafCluster(spec, emitter, context,
                               spec->rootX, trunkTop + 2, spec->rootZ,
                               1, 1, 1, shapeHash >> 8);
    }
    if (birch) {
        return EmitLeafCluster(spec, emitter, context,
                               spec->rootX, trunkTop - 4, spec->rootZ,
                               1, 2, 1, shapeHash >> 4) &&
               EmitLeafCluster(spec, emitter, context,
                               spec->rootX, trunkTop - 2, spec->rootZ,
                               2, 2, 2, shapeHash) &&
               EmitLeafCluster(spec, emitter, context,
                               spec->rootX, trunkTop + 1, spec->rootZ,
                               1, 2, 1, shapeHash >> 8);
    }
    return EmitLeafCluster(spec, emitter, context,
                           spec->rootX, trunkTop - 5, spec->rootZ,
                           2, 2, 2, shapeHash >> 4) &&
           EmitLeafCluster(spec, emitter, context,
                           spec->rootX, trunkTop - 1, spec->rootZ,
                           2, 3, 2, shapeHash) &&
           EmitLeafCluster(spec, emitter, context,
                           spec->rootX, trunkTop + 2, spec->rootZ,
                           1, 2, 1, shapeHash >> 8);
}

static bool EmitConiferWhorl(const HomeTreeShapeSpec *spec,
                             HomeTreeShapeEmitter emitter, void *context,
                             int y, int branchLength, int ringIndex,
                             bool sparse)
{
    uint32_t ringHash = ShapeWorldHash3D(
        spec->rootX, 907 + ringIndex * 23, spec->rootZ);
    int skippedDirection = sparse ? (int)(ringHash & 3u) : -1;
    int baseDirection = (int)((ringHash >> 3) & 3u);
    for (int branch = 0; branch < 4; branch++) {
        if (branch == skippedDirection) continue;
        uint32_t branchHash = ShapeWorldHash3D(
            spec->rootX, 991 + ringIndex * 31 + branch * 7,
            spec->rootZ);
        int direction = baseDirection + branch;
        int rise = branchLength >= 3 ? -1 : 0;
        if (sparse && branchLength == 1) rise = 1;
        int bend = (int)((branchHash >> 4) % 3u) - 1;
        int endX = spec->rootX;
        int endY = y;
        int endZ = spec->rootZ;
        if (!EmitBranch(spec, emitter, context, spec->rootX, y,
                        spec->rootZ, direction, branchLength, rise, bend,
                        &endX, &endY, &endZ)) return false;
        int directionX = 0;
        int directionZ = 0;
        TreeDirection(direction, &directionX, &directionZ);
        int sideX = -directionZ;
        int sideZ = directionX;
        for (int step = 1; step <= branchLength; step++) {
            int branchX = spec->rootX + directionX * step;
            int branchY = y + (rise * step) / branchLength;
            int branchZ = spec->rootZ + directionZ * step;
            if (!EmitBlock(spec, emitter, context, branchX, branchY + 1,
                           branchZ, spec->accentBlock, false)) return false;
            if (step > 1 || branchLength == 1) {
                if (!EmitBlock(spec, emitter, context,
                               branchX + sideX, branchY, branchZ + sideZ,
                               spec->accentBlock, false) ||
                    !EmitBlock(spec, emitter, context,
                               branchX - sideX, branchY, branchZ - sideZ,
                               spec->accentBlock, false)) return false;
            }
        }
        if (!EmitLeafCluster(spec, emitter, context, endX, endY, endZ,
                             1, 1, 1, branchHash)) return false;
    }
    return EmitLeafCluster(spec, emitter, context,
                           spec->rootX, y + 1, spec->rootZ,
                           1, 1, 1, ringHash);
}

static bool EmitConifer(const HomeTreeShapeSpec *spec,
                        HomeTreeShapeEmitter emitter, void *context)
{
    uint32_t shapeHash = spec->shapeHash;
    bool sparse = spec->taxonId == FLORA_TAXON_PINE;
    int trunkHeight = sparse ? 12 + (int)(shapeHash % 4u) :
        9 + (int)(shapeHash % 4u);
    for (int y = spec->baseY; y < spec->baseY + trunkHeight; y++) {
        if (!EmitBlock(spec, emitter, context, spec->rootX, y, spec->rootZ,
                       spec->primaryBlock, true)) return false;
    }
    int trunkTop = spec->baseY + trunkHeight - 1;
    int crownBase = spec->baseY + (sparse ? 5 : 2);
    int crownSpan = trunkTop - crownBase;
    int ringIndex = 0;
    for (int y = crownBase; y < trunkTop; y += 2) {
        int remaining = trunkTop - y;
        int branchLength = sparse ? 1 + remaining / crownSpan :
            1 + (remaining * 2) / crownSpan;
        if (!EmitConiferWhorl(spec, emitter, context, y, branchLength,
                              ringIndex++, sparse)) return false;
    }
    return EmitLeafCluster(spec, emitter, context,
                           spec->rootX, trunkTop, spec->rootZ,
                           1, 2, 1, shapeHash);
}

bool HomeTreeShapeEmit(const HomeTreeShapeSpec *spec,
                       HomeTreeShapeEmitter emitter, void *context)
{
    if (!spec || !emitter || spec->taxonId < FLORA_TAXON_OAK ||
        spec->taxonId > FLORA_TAXON_WILLOW) return false;
    if (spec->taxonId == FLORA_TAXON_SPRUCE ||
        spec->taxonId == FLORA_TAXON_PINE) {
        return EmitConifer(spec, emitter, context);
    }
    return EmitBroadleaf(spec, emitter, context);
}

typedef struct BoundsContext {
    HomeTreeShapeBounds bounds;
    bool hasBlock;
} BoundsContext;

static bool BoundsEmitter(void *context, int x, int y, int z,
                          BlockType block, bool replace)
{
    (void)block;
    (void)replace;
    BoundsContext *bounds = context;
    if (!bounds->hasBlock) {
        bounds->bounds = (HomeTreeShapeBounds){ x, y, z, x, y, z };
        bounds->hasBlock = true;
        return true;
    }
    if (x < bounds->bounds.minX) bounds->bounds.minX = x;
    if (y < bounds->bounds.minY) bounds->bounds.minY = y;
    if (z < bounds->bounds.minZ) bounds->bounds.minZ = z;
    if (x > bounds->bounds.maxX) bounds->bounds.maxX = x;
    if (y > bounds->bounds.maxY) bounds->bounds.maxY = y;
    if (z > bounds->bounds.maxZ) bounds->bounds.maxZ = z;
    return true;
}

bool HomeTreeShapeBoundsAt(const HomeTreeShapeSpec *spec,
                           HomeTreeShapeBounds *outBounds)
{
    if (!outBounds) return false;
    BoundsContext context = { 0 };
    if (!HomeTreeShapeEmit(spec, BoundsEmitter, &context) ||
        !context.hasBlock) return false;
    *outBounds = context.bounds;
    return true;
}

typedef struct BlockContext {
    int x;
    int y;
    int z;
    BlockType block;
    bool occupied;
} BlockContext;

static bool BlockEmitter(void *context, int x, int y, int z,
                         BlockType block, bool replace)
{
    BlockContext *target = context;
    if (x != target->x || y != target->y || z != target->z) return true;
    if (replace || !target->occupied) {
        target->block = block;
        target->occupied = true;
    }
    return true;
}

bool HomeTreeShapeBlockAt(const HomeTreeShapeSpec *spec,
                          int worldX, int y, int worldZ,
                          BlockType *outBlock)
{
    if (!spec || !outBlock) return false;
    BlockContext context = {
        .x = worldX, .y = y, .z = worldZ,
        .block = BLOCK_AIR, .occupied = false
    };
    if (!HomeTreeShapeEmit(spec, BlockEmitter, &context) ||
        !context.occupied) return false;
    *outBlock = context.block;
    return true;
}
