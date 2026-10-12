#include "world/cga_voxel.h"

#include "world/chunks.h"
#include "world/terrain.h"
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

bool CgaPlanHouseFeatures(CgaWorkspace *workspace, int cx, int cz,
                              TerrainMode mode)
{
    if (!workspace || workspace->overflow) return false;
    const int spacing = 64;
    const int halfWidth = 8;
    const int halfDepth = 8; /* Maximum extent across all four orientations. */
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(startX - halfWidth - 1, spacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + halfWidth, spacing);
    int minAnchorZ = FloorDivInt(startZ - halfDepth - 1, spacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + halfDepth, spacing);
    uint32_t seed = WorldGetSeed();
    for (int ax = minAnchorX; ax <= maxAnchorX; ax++) {
        for (int az = minAnchorZ; az <= maxAnchorZ; az++) {
            int x = ax * spacing;
            int z = az * spacing;
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
            if (workspace->featureCount >= CGA_MAX_FEATURES_PER_CHUNK) {
                workspace->overflow = true;
                return false;
            }
            workspace->features[workspace->featureCount++] = (CgaFeature){
                CgaFeatureId(seed, CGA_HOUSE_GRAMMAR_VERSION, ax, az),
                CGA_HOUSE_GRAMMAR_VERSION, seed, ax, az, scope};
        }
    }
    return true;
}

bool CgaGenerateHouseFeatures(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    if (!chunk) return false;
    CgaWorkspace workspace;
    CgaWorkspaceReset(&workspace);
    if (!CgaPlanHouseFeatures(&workspace, cx, cz, mode)) return false;
    for (size_t i = 0; i < workspace.featureCount; i++) {
        if (!CgaExpandHouse(&workspace, workspace.features[i])) return false;
    }
    return CgaEvaluateChunk(chunk, cx, cz, &workspace);
}

#ifdef TERRAIN_TESTING
size_t CgaTestPlanHouseFeatures(CgaFeature *out, size_t capacity,
                                int cx, int cz, TerrainMode mode)
{
    CgaWorkspace workspace;
    CgaWorkspaceReset(&workspace);
    if (!CgaPlanHouseFeatures(&workspace, cx, cz, mode) || !out) return 0;
    size_t count = workspace.featureCount < capacity ? workspace.featureCount : capacity;
    memcpy(out, workspace.features, count * sizeof(*out));
    return count;
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
