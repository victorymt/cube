#include "world/cga_voxel.h"

#include "world/chunks.h"
#include "world/terrain.h"
#include "world/world.h"

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
    return scope.sizeX > 0 && scope.sizeY > 0 && scope.sizeZ > 0;
}

bool CgaScopeSplit(CgaScope parent, CgaAxis axis, const int *sizes,
                   size_t count, CgaScope *out)
{
    if (!CgaScopeValid(parent) || !sizes || !out || count == 0) return false;
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
    if (!CgaScopeValid(parent) || requestedSize <= 0 || !out || capacity == 0) return 0;
    int length = ScopeLength(parent, axis);
    size_t count = (size_t)((length + requestedSize / 2) / requestedSize);
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

static CgaScope RotateScope(CgaScope scope, CgaOrientation orientation)
{
    if (orientation == CGA_ORIENTATION_0 || orientation == CGA_ORIENTATION_180) return scope;
    int size = scope.sizeX;
    scope.sizeX = scope.sizeZ;
    scope.sizeZ = size;
    return scope;
}

static bool AppendOp(CgaWorkspace *workspace, CgaOpKind kind, CgaStage stage,
                     int priority, CgaFeature feature, CgaScope scope,
                     BlockType material)
{
    if (!workspace || !CgaScopeValid(scope) || workspace->opCount >= CGA_MAX_CHUNK_OPS) {
        if (workspace) workspace->overflow = true;
        return false;
    }
    CgaVoxelOp *op = &workspace->ops[workspace->opCount];
    *op = (CgaVoxelOp){kind, stage, priority, feature.id,
                       (uint32_t)workspace->opCount, scope, material};
    workspace->opCount++;
    return true;
}

static bool ExpandHouse(CgaWorkspace *workspace, CgaFeature feature)
{
    CgaScope s = feature.scope;
    if (!CgaScopeValid(s)) return false;
    CgaScope interior = s;
    interior.minX++; interior.minY++; interior.minZ++;
    interior.sizeX -= 2; interior.sizeY -= 2; interior.sizeZ -= 2;
    CgaScope floor = interior;
    floor.sizeY = 1;
    CgaScope door = s;
    door.minX = s.minX + s.sizeX / 2 - 1;
    door.minY = s.minY + 1;
    door.minZ = s.minZ + s.sizeZ - 1;
    door.sizeX = 2; door.sizeY = 3; door.sizeZ = 1;
    CgaScope roof = s;
    roof.minY = s.minY + s.sizeY - 1;
    roof.sizeY = 1;

    if (!AppendOp(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0,
                  feature, s, BLOCK_PLANK)) return false;
    if (!AppendOp(workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, 0,
                  feature, interior, BLOCK_AIR)) return false;
    if (!AppendOp(workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, 1,
                  feature, door, BLOCK_AIR)) return false;
    if (!AppendOp(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 0,
                  feature, floor, BLOCK_WOOD)) return false;
    if (!AppendOp(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 1,
                  feature, roof, BLOCK_WOOD)) return false;

    CgaScope windows[4];
    windows[0] = (CgaScope){s.minX, s.minY + 4, s.minZ + s.sizeZ - 1, 3, 2, 1, s.orientation};
    windows[1] = (CgaScope){s.minX + s.sizeX - 3, s.minY + 4, s.minZ + s.sizeZ - 1, 3, 2, 1, s.orientation};
    windows[2] = (CgaScope){s.minX, s.minY + 4, s.minZ, 3, 2, 1, s.orientation};
    windows[3] = (CgaScope){s.minX + s.sizeX - 3, s.minY + 4, s.minZ, 3, 2, 1, s.orientation};
    for (size_t i = 0; i < 4; i++) {
        if (!AppendOp(workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, 1,
                      feature, windows[i], BLOCK_AIR) ||
            !AppendOp(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 2,
                      feature, windows[i], BLOCK_GLASS)) return false;
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

static bool ApplyWorkspace(Chunk *chunk, int cx, int cz, CgaWorkspace *workspace)
{
    if (!chunk || !workspace || workspace->overflow) return false;
    qsort(workspace->ops, workspace->opCount, sizeof(workspace->ops[0]), CompareOps);
    for (size_t i = 0; i < workspace->opCount; i++) {
        const CgaVoxelOp *op = &workspace->ops[i];
        int maxX = op->scope.minX + op->scope.sizeX;
        int maxZ = op->scope.minZ + op->scope.sizeZ;
        for (int x = op->scope.minX; x < maxX; x++) {
            for (int z = op->scope.minZ; z < maxZ; z++) {
                int localCx, localCz, lx, lz;
                WorldToChunkLocal(x, z, &localCx, &localCz, &lx, &lz);
                if (localCx != cx || localCz != cz) continue;
                for (int y = op->scope.minY; y < op->scope.minY + op->scope.sizeY; y++) {
                    if (y < SURFACE_MIN_Y || y >= SURFACE_MAX_Y_EXCLUSIVE) continue;
                    ChunkSetLocalBlock(chunk, lx, y, lz,
                                       op->kind == CGA_OP_CARVE ? BLOCK_AIR : op->material);
                }
            }
        }
    }
    return true;
}

static bool PlanHouseFeatures(CgaWorkspace *workspace, int cx, int cz,
                              TerrainMode mode)
{
    const int spacing = 64;
    const int halfWidth = 8;
    const int halfDepth = 6;
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
            if (workspace->featureCount >= CGA_MAX_FEATURES_PER_CHUNK) {
                workspace->overflow = true;
                return false;
            }
            int base = TerrainHeight(x, z, mode);
            CgaOrientation orientation = (CgaOrientation)(HomeSurfaceHashAt(x, z, 1493u) % 4u);
            CgaScope scope = {x - halfWidth, base, z - halfDepth,
                              17, 12, 13, orientation};
            scope = RotateScope(scope, orientation);
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
    if (!PlanHouseFeatures(&workspace, cx, cz, mode)) return false;
    for (size_t i = 0; i < workspace.featureCount; i++) {
        if (!ExpandHouse(&workspace, workspace.features[i])) return false;
    }
    return ApplyWorkspace(chunk, cx, cz, &workspace);
}

#ifdef TERRAIN_TESTING
bool CgaTestExpandHouse(CgaWorkspace *workspace, CgaFeature feature)
{
    return ExpandHouse(workspace, feature);
}

bool CgaTestEvaluateWorkspace(Chunk *chunk, int cx, int cz,
                              CgaWorkspace *workspace)
{
    return ApplyWorkspace(chunk, cx, cz, workspace);
}
#endif
