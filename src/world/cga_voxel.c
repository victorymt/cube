#include "world/cga_voxel.h"

#include "world/chunks.h"
#include "world/terrain.h"
#include "world/terrain_generation.h"
#include "world/world.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int ScopeLength(CgaScope scope, CgaAxis axis)
{
    return axis == CGA_AXIS_X ? scope.sizeX :
           axis == CGA_AXIS_Y ? scope.sizeY : scope.sizeZ;
}

static void ScopeSetAxis(CgaScope *scope, CgaAxis axis, int min, int size)
{
    if (axis == CGA_AXIS_X) { scope->minX = min; scope->sizeX = size; }
    else if (axis == CGA_AXIS_Y) { scope->minY = min; scope->sizeY = size; }
    else { scope->minZ = min; scope->sizeZ = size; }
}

void CgaWorkspaceReset(CgaWorkspace *workspace)
{
    if (workspace) memset(workspace, 0, sizeof(*workspace));
}

bool CgaScopeValid(CgaScope scope)
{
    return scope.sizeX > 0 && scope.sizeY > 0 && scope.sizeZ > 0 &&
           scope.minX <= INT_MAX - scope.sizeX &&
           scope.minY <= INT_MAX - scope.sizeY &&
           scope.minZ <= INT_MAX - scope.sizeZ &&
           scope.orientation >= CGA_ORIENTATION_0 &&
           scope.orientation <= CGA_ORIENTATION_270;
}

bool CgaScopeSplit(CgaScope parent, CgaAxis axis, const int *sizes,
                   size_t count, CgaScope *out)
{
    if (!CgaScopeValid(parent) || axis < CGA_AXIS_X || axis > CGA_AXIS_Z || !sizes || !out || count == 0) return false;
    int total = 0;
    int length = ScopeLength(parent, axis);
    for (size_t i = 0; i < count; i++) {
        if (sizes[i] <= 0 || sizes[i] > length - total) return false;
        total += sizes[i];
    }
    if (total != length) return false;
    int cursor = axis == CGA_AXIS_X ? parent.minX :
                 axis == CGA_AXIS_Y ? parent.minY : parent.minZ;
    for (size_t i = 0; i < count; i++) {
        out[i] = parent;
        ScopeSetAxis(&out[i], axis, cursor, sizes[i]);
        cursor += sizes[i];
    }
    return true;
}

size_t CgaScopeRepeat(CgaScope parent, CgaAxis axis, int requestedSize,
                      CgaScope *out, size_t capacity)
{
    if (!CgaScopeValid(parent) || axis < CGA_AXIS_X || axis > CGA_AXIS_Z || requestedSize <= 0 || !out || capacity == 0) return 0;
    int length = ScopeLength(parent, axis);
    size_t count = (size_t)(((int64_t)length + requestedSize / 2) / requestedSize);
    if (count < 1) count = 1;
    if (count > capacity) return 0;
    int base = length / (int)count;
    int remainder = length % (int)count;
    int cursor = axis == CGA_AXIS_X ? parent.minX :
                 axis == CGA_AXIS_Y ? parent.minY : parent.minZ;
    for (size_t i = 0; i < count; i++) {
        int size = base + ((int)i >= (int)count - remainder ? 1 : 0);
        out[i] = parent;
        ScopeSetAxis(&out[i], axis, cursor, size);
        cursor += size;
    }
    return count;
}

static uint64_t Mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static bool ScopeOverlapsXZ(CgaScope left, CgaScope right)
{
    return left.minX < right.minX + right.sizeX &&
           right.minX < left.minX + left.sizeX &&
           left.minZ < right.minZ + right.sizeZ &&
           right.minZ < left.minZ + left.sizeZ;
}

static int CompareFeatures(const void *a, const void *b)
{
    const CgaFeature *left = a;
    const CgaFeature *right = b;
    if (left->id != right->id) return left->id < right->id ? -1 : 1;
    if (left->anchorX != right->anchorX) return left->anchorX < right->anchorX ? -1 : 1;
    if (left->anchorZ != right->anchorZ) return left->anchorZ < right->anchorZ ? -1 : 1;
    return 0;
}

static size_t ResolveFeatureConflicts(CgaFeature *features, size_t count,
                                      const CgaContext *context)
{
    if (!features) return 0;
    qsort(features, count, sizeof(*features), CompareFeatures);
    size_t accepted = 0;
    for (size_t i = 0; i < count; i++) {
        CgaFeature candidate = features[i];
        if (context && context->regionClear &&
            !context->regionClear(context->userData, candidate.scope, candidate.id)) {
            continue;
        }
        bool conflict = false;
        for (size_t j = 0; j < accepted; j++) {
            if (ScopeOverlapsXZ(candidate.scope, features[j].scope)) {
                conflict = true;
                break;
            }
        }
        if (!conflict) features[accepted++] = candidate;
    }
    return accepted;
}

static bool HouseTerrainSuitable(CgaScope scope, int base, TerrainMode mode)
{
    int minHeight = INT_MAX;
    int maxHeight = INT_MIN;
    for (int x = scope.minX; x < scope.minX + scope.sizeX; x++) {
        for (int z = scope.minZ; z < scope.minZ + scope.sizeZ; z++) {
            int height = TerrainHeight(x, z, mode);
            if (height < minHeight) minHeight = height;
            if (height > maxHeight) maxHeight = height;
            if (height < base - 1 || height > base + 1) return false;
        }
    }
    return maxHeight - minHeight <= 1;
}

uint64_t CgaFeatureId(uint32_t worldSeed, uint32_t grammarVersion,
                      int anchorX, int anchorZ)
{
    uint64_t x = ((uint64_t)worldSeed << 32) | grammarVersion;
    x ^= (uint64_t)(uint32_t)anchorX * 0x9e3779b1U;
    x ^= (uint64_t)(uint32_t)anchorZ * 0x85ebca77U;
    return Mix64(x);
}

/* Convert a local half-open box to the rotated world AABB. */
static CgaScope LocalBox(CgaScope parent, int x, int y, int z,
                         int width, int height, int depth)
{
    CgaScope box = {parent.minX, parent.minY + y, parent.minZ,
                    width, height, depth, CGA_ORIENTATION_0};
    int localWidth = (parent.orientation & 1) ? parent.sizeZ : parent.sizeX;
    int localDepth = (parent.orientation & 1) ? parent.sizeX : parent.sizeZ;
    switch (parent.orientation) {
        case CGA_ORIENTATION_0:
            box.minX += x; box.minZ += z;
            break;
        case CGA_ORIENTATION_90:
            box.minX += localDepth - z - depth; box.minZ += x;
            box.sizeX = depth; box.sizeZ = width;
            break;
        case CGA_ORIENTATION_180:
            box.minX += parent.sizeX - x - width;
            box.minZ += parent.sizeZ - z - depth;
            break;
        case CGA_ORIENTATION_270:
            box.minX += z; box.minZ += localWidth - x - width;
            box.sizeX = depth; box.sizeZ = width;
            break;
    }
    return box;
}

bool CgaAppendOp(CgaWorkspace *workspace, CgaOpKind kind, CgaStage stage,
                     int priority, CgaFeature feature, CgaScope scope,
                     BlockType material)
{
    if (!workspace || workspace->overflow || !CgaScopeValid(scope) ||
        kind < CGA_OP_FILL || kind > CGA_OP_PAINT ||
        stage < CGA_STAGE_BASE || stage > CGA_STAGE_PAINT ||
        workspace->opCount >= CGA_MAX_CHUNK_OPS) {
        if (workspace) workspace->overflow = true;
        return false;
    }
    CgaVoxelOp *op = &workspace->ops[workspace->opCount];
    *op = (CgaVoxelOp){kind, stage, priority, feature.id,
                       (uint32_t)workspace->opCount, scope, material};
    workspace->opCount++;
    return true;
}

bool CgaExpandHouse(CgaWorkspace *workspace, CgaFeature feature)
{
    CgaScope s = feature.scope;
    int width = (s.orientation & 1) ? s.sizeZ : s.sizeX;
    int depth = (s.orientation & 1) ? s.sizeX : s.sizeZ;
    if (!workspace || !CgaScopeValid(s) || width < 9 || depth < 5 || s.sizeY < 8 ||
        feature.grammarVersion != CGA_HOUSE_GRAMMAR_VERSION) return false;
    CgaScope world = LocalBox(s, 0, 0, 0, width, s.sizeY, depth);
    /* Reserve the entire rule expansion, so capacity failure cannot emit half a house. */
    if (workspace->overflow || workspace->opCount > CGA_MAX_CHUNK_OPS - 9) {
        workspace->overflow = true;
        return false;
    }
    CgaScope interior = LocalBox(s, 1, 1, 1, width - 2, s.sizeY - 2, depth - 2);
    CgaScope floor = LocalBox(s, 0, 0, 0, width, 1, depth);
    CgaScope door = LocalBox(s, width / 2 - 1, 1, depth - 1, 2, 3, 1);
    CgaScope roof = LocalBox(s, 0, s.sizeY - 1, 0, width, 1, depth);

    if (!CgaAppendOp(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0,
                     feature, world, BLOCK_PLANK) ||
        !CgaAppendOp(workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, 0,
                     feature, interior, BLOCK_AIR) ||
        !CgaAppendOp(workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, 1,
                     feature, door, BLOCK_AIR) ||
        !CgaAppendOp(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 0,
                     feature, floor, BLOCK_WOOD) ||
        !CgaAppendOp(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 1,
                     feature, roof, BLOCK_WOOD)) return false;

    /* Paint existing wall voxels; painting never fills a carved opening. */
    for (int side = 0; side < 2; side++) {
        for (int slot = 0; slot < 2; slot++) {
            CgaScope window = LocalBox(s, slot ? width - 4 : 1, 4,
                                        side ? depth - 1 : 0, 3, 2, 1);
            if (!CgaAppendOp(workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, 1,
                             feature, window, BLOCK_AIR) ||
                !CgaAppendOp(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 2,
                             feature, window, BLOCK_GLASS)) return false;
        }
    }
    return true;
}

static bool AppendWorldBox(CgaWorkspace *workspace, CgaOpKind kind,
                           CgaStage stage, int priority, CgaFeature feature,
                           int minX, int minY, int minZ,
                           int sizeX, int sizeY, int sizeZ, BlockType material)
{
    return CgaAppendOp(workspace, kind, stage, priority, feature,
                       (CgaScope){minX, minY, minZ, sizeX, sizeY, sizeZ,
                                  CGA_ORIENTATION_0}, material);
}

static bool ExpandMineshaft(CgaWorkspace *workspace, CgaFeature feature)
{
    if (!workspace || feature.grammarVersion != CGA_MINESHAFT_GRAMMAR_VERSION ||
        !CgaScopeValid(feature.scope)) return false;
    int dx = feature.scope.orientation == CGA_ORIENTATION_0 ||
             feature.scope.orientation == CGA_ORIENTATION_270 ? 1 : -1;
    int dz = feature.scope.orientation == CGA_ORIENTATION_0 ||
             feature.scope.orientation == CGA_ORIENTATION_90 ? 1 : -1;
    int startX = feature.anchorX * 64;
    int startZ = feature.anchorZ * 64;
    int length = feature.scope.sizeX > feature.scope.sizeZ
        ? feature.scope.sizeX : feature.scope.sizeZ;
    int y = feature.scope.minY;
    if (workspace->overflow || workspace->opCount > CGA_MAX_CHUNK_OPS -
        (size_t)(length + length / 4 + 1)) {
        workspace->overflow = true;
        return false;
    }
    for (int i = 0; i < length; i++) {
        int bx = startX + dx * i;
        int bz = startZ + dz * i;
        if (!AppendWorldBox(workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, 0,
                            feature, bx, y, bz, 1, 3, 1, BLOCK_AIR)) return false;
        if (i % 4 == 0 && !AppendWorldBox(
                workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 0, feature,
                bx, y, bz, 1, 4, 1, BLOCK_WOOD)) return false;
    }
    return true;
}

static bool ExpandDungeon(CgaWorkspace *workspace, CgaFeature feature)
{
    if (!workspace || feature.grammarVersion != CGA_DUNGEON_GRAMMAR_VERSION ||
        !CgaScopeValid(feature.scope)) return false;
    CgaScope s = feature.scope;
    int left = s.minX;
    int right = s.minX + s.sizeX - 1;
    int front = s.minZ;
    int back = s.minZ + s.sizeZ - 1;
    if (!AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        left, s.minY, front, 1, 4, s.sizeZ, BLOCK_STONE_BRICKS) ||
        !AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        right, s.minY, front, 1, 4, s.sizeZ, BLOCK_STONE_BRICKS) ||
        !AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        left + 1, s.minY, back, s.sizeX - 2, 4, 1,
                        BLOCK_STONE_BRICKS) ||
        !AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        left, s.minY, front, 2, 4, 1, BLOCK_STONE_BRICKS) ||
        !AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        right - 2, s.minY, front, 3, 4, 1,
                        BLOCK_STONE_BRICKS) ||
        !AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        left, s.minY, s.minZ + 3, 1, 4, 1,
                        BLOCK_STONE_BRICKS) ||
        !AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        right, s.minY, s.minZ + 3, 1, 4, 1,
                        BLOCK_STONE_BRICKS) ||
        !AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        s.minX + 3, s.minY, back, 1, 4, 1,
                        BLOCK_STONE_BRICKS)) return false;
    return true;
}

static bool ExpandDesertTemple(CgaWorkspace *workspace, CgaFeature feature)
{
    if (!workspace || feature.grammarVersion != CGA_DESERT_TEMPLE_GRAMMAR_VERSION ||
        !CgaScopeValid(feature.scope)) return false;
    CgaScope s = feature.scope;
    int x = feature.anchorX * 128;
    int z = feature.anchorZ * 128;
    int base = s.minY + 4;
    if (!AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        x - 2, base + 1, z - 2, 5, 3, 5,
                        BLOCK_SANDSTONE) ||
        !AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        x, base + 4, z, 1, 1, 1, BLOCK_SANDSTONE) ||
        !AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        x - 4, base + 1, z, 1, 4, 1, BLOCK_SANDSTONE) ||
        !AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        x + 4, base + 1, z, 1, 4, 1, BLOCK_SANDSTONE) ||
        !AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        x, base + 1, z - 4, 1, 4, 1, BLOCK_SANDSTONE) ||
        !AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        x, base + 1, z + 4, 1, 4, 1, BLOCK_SANDSTONE) ||
        !AppendWorldBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        x - 1, base - 4, z - 1, 3, 4, 3, BLOCK_SANDSTONE)) return false;
    return true;
}

bool CgaExpandFeature(CgaWorkspace *workspace, CgaFeature feature)
{
    switch (feature.grammarVersion) {
        case CGA_HOUSE_GRAMMAR_VERSION: return CgaExpandHouse(workspace, feature);
        case CGA_MINESHAFT_GRAMMAR_VERSION: return ExpandMineshaft(workspace, feature);
        case CGA_DUNGEON_GRAMMAR_VERSION: return ExpandDungeon(workspace, feature);
        case CGA_DESERT_TEMPLE_GRAMMAR_VERSION: return ExpandDesertTemple(workspace, feature);
        default: return false;
    }
}

static int CompareOps(const void *a, const void *b)
{
    const CgaVoxelOp *left = a;
    const CgaVoxelOp *right = b;
#define CMP(field) if (left->field != right->field) return left->field < right->field ? -1 : 1
    CMP(stage); CMP(priority);
    if (left->featureId != right->featureId) return left->featureId < right->featureId ? -1 : 1;
    CMP(sequence);
#undef CMP
    return 0;
}

bool CgaEvaluateChunk(Chunk *chunk, int cx, int cz, CgaWorkspace *workspace)
{
    if (!chunk || !workspace || workspace->overflow) return false;
    qsort(workspace->ops, workspace->opCount, sizeof(workspace->ops[0]), CompareOps);
    for (size_t i = 0; i < workspace->opCount; i++) {
        const CgaVoxelOp *op = &workspace->ops[i];
        int minX = op->scope.minX > cx * CHUNK_SIZE ? op->scope.minX : cx * CHUNK_SIZE;
        int minZ = op->scope.minZ > cz * CHUNK_SIZE ? op->scope.minZ : cz * CHUNK_SIZE;
        int maxX = op->scope.minX + op->scope.sizeX;
        int maxZ = op->scope.minZ + op->scope.sizeZ;
        if (maxX > cx * CHUNK_SIZE + CHUNK_SIZE) maxX = cx * CHUNK_SIZE + CHUNK_SIZE;
        if (maxZ > cz * CHUNK_SIZE + CHUNK_SIZE) maxZ = cz * CHUNK_SIZE + CHUNK_SIZE;
        int minY = op->scope.minY > SURFACE_MIN_Y ? op->scope.minY : SURFACE_MIN_Y;
        int maxY = op->scope.minY + op->scope.sizeY;
        if (maxY > SURFACE_MAX_Y_EXCLUSIVE) maxY = SURFACE_MAX_Y_EXCLUSIVE;
        for (int x = minX; x < maxX; x++) {
            for (int z = minZ; z < maxZ; z++) {
                int lx = x - cx * CHUNK_SIZE, lz = z - cz * CHUNK_SIZE;
                for (int y = minY; y < maxY; y++) {
                    if (!ChunkSetLocalBlock(chunk, lx, y, lz,
                            op->kind == CGA_OP_CARVE ? BLOCK_AIR : op->material)) return false;
                }
            }
        }
    }
    return true;
}

static bool AddFeature(CgaWorkspace *workspace, uint32_t grammarVersion,
                       uint32_t seed, int anchorX, int anchorZ,
                       CgaScope scope)
{
    if (workspace->featureCount >= CGA_MAX_FEATURES_PER_CHUNK) {
        workspace->overflow = true;
        return false;
    }
    workspace->features[workspace->featureCount++] = (CgaFeature){
        CgaFeatureId(seed, grammarVersion, anchorX, anchorZ),
        grammarVersion, seed, anchorX, anchorZ, scope};
    return true;
}

#ifdef TERRAIN_TESTING
static bool PlanMineshaftOnly(CgaWorkspace *workspace, int cx, int cz,
                              const CgaContext *context)
{
    const int spacing = 64;
    const int extent = 20;
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(startX - extent, spacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + extent, spacing);
    int minAnchorZ = FloorDivInt(startZ - extent, spacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + extent, spacing);
    uint32_t seed = WorldGetSeed();
    for (int ax = minAnchorX; ax <= maxAnchorX; ax++) {
        for (int az = minAnchorZ; az <= maxAnchorZ; az++) {
            int x = ax * spacing;
            int z = az * spacing;
            if (HomeSurfaceHashAt(x, z, 1409u) % 100u >= 75u) continue;
            int y = 8 + (int)(HomeSurfaceHashAt(x, z, 1423u) % 5u);
            int length = 12 + (int)(HomeSurfaceHashAt(x, z, 1433u) % 9u);
            int dx = (HomeSurfaceHashAt(x, z, 1427u) % 2u) ? 1 : -1;
            int dz = (HomeSurfaceHashAt(x, z, 1429u) % 2u) ? 1 : -1;
            int endX = x + dx * (length - 1);
            int endZ = z + dz * (length - 1);
            CgaScope scope = {
                endX < x ? endX : x, y, endZ < z ? endZ : z,
                abs(endX - x) + 1, 4, abs(endZ - z) + 1,
                (dx > 0 && dz > 0) ? CGA_ORIENTATION_0 :
                (dx < 0 && dz > 0) ? CGA_ORIENTATION_90 :
                (dx < 0 && dz < 0) ? CGA_ORIENTATION_180 :
                CGA_ORIENTATION_270};
            if (!AddFeature(workspace, CGA_MINESHAFT_GRAMMAR_VERSION, seed,
                            ax, az, scope)) return false;
        }
    }
    workspace->featureCount = ResolveFeatureConflicts(workspace->features,
                                                      workspace->featureCount,
                                                      context);
    return true;
}
#endif

bool CgaPlanStructureFeatures(CgaWorkspace *workspace, int cx, int cz,
                              TerrainMode mode, const CgaContext *context)
{
    if (!workspace || workspace->overflow) return false;
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    uint32_t seed = WorldGetSeed();

    const int houseSpacing = 64;
    const int houseHalfExtent = 9;
    int minAnchorX = FloorDivInt(startX - houseHalfExtent, houseSpacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + houseHalfExtent, houseSpacing);
    int minAnchorZ = FloorDivInt(startZ - houseHalfExtent, houseSpacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + houseHalfExtent, houseSpacing);
    for (int ax = minAnchorX; ax <= maxAnchorX; ax++) {
        for (int az = minAnchorZ; az <= maxAnchorZ; az++) {
            int x = ax * houseSpacing;
            int z = az * houseSpacing;
            if (HomeSurfaceHashAt(x, z, 1481u) % 100u >= 62u ||
                BiomeAt(x, z) == BIOME_DESERT || TerrainHeight(x, z, mode) < 4) continue;
            int base = TerrainHeight(x, z, mode);
            CgaOrientation orientation = (CgaOrientation)(HomeSurfaceHashAt(x, z, 1493u) % 4u);
            int width = (orientation & 1) ? 13 : 17;
            int depth = (orientation & 1) ? 17 : 13;
            CgaScope scope = {x - width / 2, base, z - depth / 2,
                              width, 12, depth, orientation};
            if (scope.minX >= startX + CHUNK_SIZE || scope.minX + scope.sizeX <= startX ||
                scope.minZ >= startZ + CHUNK_SIZE || scope.minZ + scope.sizeZ <= startZ) continue;
            if (!HouseTerrainSuitable(scope, base, mode)) continue;
            if (!AddFeature(workspace, CGA_HOUSE_GRAMMAR_VERSION, seed,
                            ax, az, scope)) return false;
        }
    }

    const int shaftSpacing = 64;
    const int shaftExtent = 20;
    minAnchorX = FloorDivInt(startX - shaftExtent, shaftSpacing);
    maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + shaftExtent, shaftSpacing);
    minAnchorZ = FloorDivInt(startZ - shaftExtent, shaftSpacing);
    maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + shaftExtent, shaftSpacing);
    for (int ax = minAnchorX; ax <= maxAnchorX; ax++) {
        for (int az = minAnchorZ; az <= maxAnchorZ; az++) {
            int x = ax * shaftSpacing;
            int z = az * shaftSpacing;
            if (HomeSurfaceHashAt(x, z, 1409u) % 100u >= 75u) continue;
            int y = 8 + (int)(HomeSurfaceHashAt(x, z, 1423u) % 5u);
            int length = 12 + (int)(HomeSurfaceHashAt(x, z, 1433u) % 9u);
            int dx = (HomeSurfaceHashAt(x, z, 1427u) % 2u) ? 1 : -1;
            int dz = (HomeSurfaceHashAt(x, z, 1429u) % 2u) ? 1 : -1;
            int endX = x + dx * (length - 1);
            int endZ = z + dz * (length - 1);
            CgaScope scope = {
                endX < x ? endX : x, y, endZ < z ? endZ : z,
                abs(endX - x) + 1, 4, abs(endZ - z) + 1,
                (dx > 0 && dz > 0) ? CGA_ORIENTATION_0 :
                (dx < 0 && dz > 0) ? CGA_ORIENTATION_90 :
                (dx < 0 && dz < 0) ? CGA_ORIENTATION_180 :
                CGA_ORIENTATION_270};
            if (scope.minX >= startX + CHUNK_SIZE || scope.minX + scope.sizeX <= startX ||
                scope.minZ >= startZ + CHUNK_SIZE || scope.minZ + scope.sizeZ <= startZ) continue;
            if (!AddFeature(workspace, CGA_MINESHAFT_GRAMMAR_VERSION, seed,
                            ax, az, scope)) return false;
        }
    }

    const int dungeonSpacing = 128;
    const int dungeonExtent = 6;
    minAnchorX = FloorDivInt(startX - dungeonExtent, dungeonSpacing);
    maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + dungeonExtent, dungeonSpacing);
    minAnchorZ = FloorDivInt(startZ - dungeonExtent, dungeonSpacing);
    maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + dungeonExtent, dungeonSpacing);
    for (int ax = minAnchorX; ax <= maxAnchorX; ax++) {
        for (int az = minAnchorZ; az <= maxAnchorZ; az++) {
            int x = ax * dungeonSpacing;
            int z = az * dungeonSpacing;
            if (HomeSurfaceHashAt(x, z, 1451u) % 100u >= 64u) continue;
            CgaScope scope = {x - 3, 10 + (int)(HomeSurfaceHashAt(x, z, 1453u) % 4u),
                              z - 3, 7, 4, 7, CGA_ORIENTATION_0};
            if (scope.minX >= startX + CHUNK_SIZE || scope.minX + scope.sizeX <= startX ||
                scope.minZ >= startZ + CHUNK_SIZE || scope.minZ + scope.sizeZ <= startZ) continue;
            if (!AddFeature(workspace, CGA_DUNGEON_GRAMMAR_VERSION, seed,
                            ax, az, scope)) return false;
        }
    }

    const int templeSpacing = 128;
    const int templeExtent = 7;
    minAnchorX = FloorDivInt(startX - templeExtent, templeSpacing);
    maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + templeExtent, templeSpacing);
    minAnchorZ = FloorDivInt(startZ - templeExtent, templeSpacing);
    maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + templeExtent, templeSpacing);
    for (int ax = minAnchorX; ax <= maxAnchorX; ax++) {
        for (int az = minAnchorZ; az <= maxAnchorZ; az++) {
            int x = ax * templeSpacing;
            int z = az * templeSpacing;
            if (BiomeAt(x, z) != BIOME_DESERT ||
                HomeSurfaceHashAt(x, z, 1471u) % 100u >= 40u) continue;
            int base = TerrainHeight(x, z, mode);
            CgaScope scope = {x - 4, base - 4, z - 4, 9, 9, 9,
                              CGA_ORIENTATION_0};
            if (scope.minX >= startX + CHUNK_SIZE || scope.minX + scope.sizeX <= startX ||
                scope.minZ >= startZ + CHUNK_SIZE || scope.minZ + scope.sizeZ <= startZ) continue;
            if (!AddFeature(workspace, CGA_DESERT_TEMPLE_GRAMMAR_VERSION, seed,
                            ax, az, scope)) return false;
        }
    }
    workspace->featureCount = ResolveFeatureConflicts(workspace->features,
                                                      workspace->featureCount,
                                                      context);
    return true;
}

bool CgaPlanHouseFeatures(CgaWorkspace *workspace, int cx, int cz,
                          TerrainMode mode, const CgaContext *context)
{
    if (!workspace) return false;
    CgaWorkspace all;
    CgaWorkspaceReset(&all);
    if (!CgaPlanStructureFeatures(&all, cx, cz, mode, context)) {
        *workspace = all;
        return false;
    }
    CgaWorkspaceReset(workspace);
    for (size_t i = 0; i < all.featureCount; i++) {
        if (all.features[i].grammarVersion == CGA_HOUSE_GRAMMAR_VERSION) {
            if (workspace->featureCount >= CGA_MAX_FEATURES_PER_CHUNK) {
                workspace->overflow = true;
                return false;
            }
            workspace->features[workspace->featureCount++] = all.features[i];
        }
    }
    return true;
}

bool CgaGenerateHouseFeatures(Chunk *chunk, int cx, int cz, TerrainMode mode,
                              const CgaContext *context)
{
    if (!chunk) return false;
    CgaWorkspace workspace;
    CgaWorkspaceReset(&workspace);
    if (!CgaPlanHouseFeatures(&workspace, cx, cz, mode, context)) return false;
    for (size_t i = 0; i < workspace.featureCount; i++) {
        if (!CgaExpandHouse(&workspace, workspace.features[i])) return false;
    }
    return CgaEvaluateChunk(chunk, cx, cz, &workspace);
}

static void MaterializeMineshaftTerrain(const CgaWorkspace *workspace,
                                        Chunk *chunk, int cx, int cz,
                                        TerrainMode mode)
{
    for (size_t i = 0; i < workspace->featureCount; i++) {
        const CgaFeature *feature = &workspace->features[i];
        if (feature->grammarVersion != CGA_MINESHAFT_GRAMMAR_VERSION) continue;
        int first = SurfaceSectionYFromBlockY(feature->scope.minY);
        int last = SurfaceSectionYFromBlockY(feature->scope.minY + 3);
        for (int sectionY = first; sectionY <= last; sectionY++) {
            GenerateChunkTerrainSectionBase(chunk, cx, cz, sectionY, mode);
        }
    }
}

bool CgaGenerateStructureFeatures(Chunk *chunk, int cx, int cz,
                                  TerrainMode mode, const CgaContext *context)
{
    if (!chunk) return false;
    CgaWorkspace workspace;
    CgaWorkspaceReset(&workspace);
    if (!CgaPlanStructureFeatures(&workspace, cx, cz, mode, context)) return false;
    MaterializeMineshaftTerrain(&workspace, chunk, cx, cz, mode);
    for (size_t i = 0; i < workspace.featureCount; i++) {
        if (!CgaExpandFeature(&workspace, workspace.features[i])) return false;
    }
    return CgaEvaluateChunk(chunk, cx, cz, &workspace);
}

#ifdef TERRAIN_TESTING
bool CgaGenerateMineshaftFeatures(Chunk *chunk, int cx, int cz,
                                  TerrainMode mode,
                                  const CgaContext *context)
{
    if (!chunk) return false;
    CgaWorkspace workspace;
    CgaWorkspaceReset(&workspace);
    if (!PlanMineshaftOnly(&workspace, cx, cz, context)) return false;
    MaterializeMineshaftTerrain(&workspace, chunk, cx, cz, mode);
    for (size_t i = 0; i < workspace.featureCount; i++) {
        if (!CgaExpandFeature(&workspace, workspace.features[i])) return false;
    }
    return CgaEvaluateChunk(chunk, cx, cz, &workspace);
}

size_t CgaTestPlanHouseFeatures(CgaFeature *out, size_t capacity,
                                int cx, int cz, TerrainMode mode)
{
    return CgaTestPlanHouseFeaturesWithContext(out, capacity, cx, cz, mode,
                                               NULL);
}

size_t CgaTestPlanHouseFeaturesWithContext(CgaFeature *out, size_t capacity,
                                           int cx, int cz, TerrainMode mode,
                                           const CgaContext *context)
{
    CgaWorkspace workspace;
    CgaWorkspaceReset(&workspace);
    if (!CgaPlanHouseFeatures(&workspace, cx, cz, mode, context) || !out) return 0;
    size_t count = workspace.featureCount < capacity ? workspace.featureCount : capacity;
    memcpy(out, workspace.features, count * sizeof(*out));
    return count;
}

size_t CgaTestResolveFeatureConflicts(CgaFeature *features, size_t count,
                                      const CgaContext *context)
{
    return ResolveFeatureConflicts(features, count, context);
}

bool CgaTestExpandHouse(CgaWorkspace *workspace, CgaFeature feature)
{
    return CgaExpandHouse(workspace, feature);
}

bool CgaTestEvaluateWorkspace(Chunk *chunk, int cx, int cz,
                              CgaWorkspace *workspace)
{
    return CgaEvaluateChunk(chunk, cx, cz, workspace);
}
#endif
