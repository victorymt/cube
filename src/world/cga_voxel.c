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

static bool StructureTerrainSuitable(CgaScope scope, int base, TerrainMode mode,
                                     int tolerance)
{
    int minHeight = INT_MAX;
    int maxHeight = INT_MIN;
    for (int x = scope.minX; x < scope.minX + scope.sizeX; x++) {
        for (int z = scope.minZ; z < scope.minZ + scope.sizeZ; z++) {
            int height = TerrainHeight(x, z, mode);
            if (height < minHeight) minHeight = height;
            if (height > maxHeight) maxHeight = height;
            if (height < base - tolerance || height > base + tolerance) return false;
        }
    }
    return maxHeight - minHeight <= tolerance;
}

static bool HouseTerrainSuitable(CgaScope scope, int base, TerrainMode mode)
{
    return StructureTerrainSuitable(scope, base, mode, 1);
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
    /* Reserve the entire rule expansion, so capacity failure cannot emit half a house. */
    /* The house grammar expands into a raised stone base, plastered timber
     * frame, doors/windows and three stepped roof courses. */
    if (workspace->overflow || workspace->opCount > CGA_MAX_CHUNK_OPS - 40) {
        workspace->overflow = true;
        return false;
    }
    int wallHeight = s.sizeY - 4;
    CgaScope foundation = LocalBox(s, -1, 0, -1, width + 2, 1, depth + 2);
    CgaScope walls = LocalBox(s, 0, 1, 0, width, wallHeight, depth);
    CgaScope interior = LocalBox(s, 1, 2, 1, width - 2, wallHeight - 1, depth - 2);
    CgaScope floor = LocalBox(s, 0, 1, 0, width, 1, depth);
    CgaScope door = LocalBox(s, width / 2 - 1, 2, depth - 1, 2, 3, 1);
    CgaScope eave = LocalBox(s, -2, s.sizeY - 3, -2, width + 4, 1, depth + 4);
    CgaScope upperRoof = LocalBox(s, -1, s.sizeY - 2, -1, width + 2, 1, depth + 2);
    CgaScope ridge = LocalBox(s, 1, s.sizeY - 1, 1, width - 2, 1, depth - 2);

    if (!CgaAppendOp(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0,
                     feature, foundation, BLOCK_STONE_BRICKS) ||
        !CgaAppendOp(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 1,
                     feature, walls, BLOCK_WHITE) ||
        !CgaAppendOp(workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, 0,
                     feature, interior, BLOCK_AIR) ||
        !CgaAppendOp(workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, 1,
                     feature, door, BLOCK_AIR) ||
        !CgaAppendOp(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 0,
                     feature, floor, BLOCK_WOOD) ||
        !CgaAppendOp(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 2,
                     feature, eave, BLOCK_STONE_STAIRS) ||
        !CgaAppendOp(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 3,
                     feature, upperRoof, BLOCK_STONE_STAIRS) ||
        !CgaAppendOp(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 4,
                     feature, ridge, BLOCK_STONE_STAIRS)) return false;

    /* The regular red posts and beams expose the timber frame through the
     * white infill walls, while the wide eaves give the roof its Chinese form. */
    for (int side = 0; side < 2; side++) {
        for (int end = 0; end < 2; end++) {
            int x = end ? width - 1 : 0;
            int z = side ? depth - 1 : 0;
            if (!CgaAppendOp(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 1,
                             feature, LocalBox(s, x, 1, z, 1, wallHeight, 1),
                             BLOCK_RED)) return false;
        }
    }
    if (!CgaAppendOp(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 1, feature,
                     LocalBox(s, 0, wallHeight - 1, 0, width, 1, 1), BLOCK_RED) ||
        !CgaAppendOp(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 1, feature,
                     LocalBox(s, 0, wallHeight - 1, depth - 1, width, 1, 1), BLOCK_RED) ||
        !CgaAppendOp(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 1, feature,
                     LocalBox(s, 0, 1, 0, 1, 1, depth), BLOCK_RED) ||
        !CgaAppendOp(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 1, feature,
                     LocalBox(s, width - 1, 1, 0, 1, 1, depth), BLOCK_RED)) return false;

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

static bool ReserveOps(CgaWorkspace *workspace, size_t count)
{
    if (!workspace || workspace->overflow || count > CGA_MAX_CHUNK_OPS ||
        workspace->opCount > CGA_MAX_CHUNK_OPS - count) {
        if (workspace) workspace->overflow = true;
        return false;
    }
    return true;
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
    if (!ReserveOps(workspace, (size_t)(length + length / 4 + 1))) return false;
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
    if (!ReserveOps(workspace, 8)) return false;
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
    if (!ReserveOps(workspace, 7)) return false;
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

static bool AppendLocalBox(CgaWorkspace *workspace, CgaOpKind kind,
                           CgaStage stage, int priority, CgaFeature feature,
                           CgaScope parent, int x, int y, int z,
                           int width, int height, int depth,
                           BlockType material)
{
    return CgaAppendOp(workspace, kind, stage, priority, feature,
                       LocalBox(parent, x, y, z, width, height, depth),
                       material);
}

/*
 * A rare five-bay hall assembled from the same voxel vocabulary as the
 * smaller house grammar.  The composition follows a Chinese timber hall:
 * stone terrace, open front colonnade, plaster infill, repeated red columns,
 * a green/gold bracket band, lattice openings, and a stepped hip roof whose
 * corners visibly turn upward.
 */
static bool ExpandClassicalHall(CgaWorkspace *workspace, CgaFeature feature)
{
    CgaScope s = feature.scope;
    int width = (s.orientation & 1) ? s.sizeZ : s.sizeX;
    int depth = (s.orientation & 1) ? s.sizeX : s.sizeZ;
    if (!workspace || feature.grammarVersion != CGA_CLASSICAL_HALL_GRAMMAR_VERSION ||
        !CgaScopeValid(s) || width < 29 || depth < 27 || s.sizeY < 19) return false;
    if (!ReserveOps(workspace, 220)) return false;

    /* Two buried foundation courses settle the hall into gentle terrain. */
    if (!AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 0, feature,
                        s, 1, -2, 1, 27, 3, 25, BLOCK_STONE_BRICKS) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 1, feature,
                        s, 2, 1, 2, 25, 1, 23, BLOCK_DRESSED_STONE)) return false;

    /* Broad front steps, with the centre kept visually quiet like a stone ramp. */
    if (!AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 2, feature,
                        s, 11, 0, 0, 7, 1, 1, BLOCK_DRESSED_STONE) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 3, feature,
                        s, 11, 0, 1, 7, 2, 1, BLOCK_DRESSED_STONE)) return false;

    /* Main enclosed volume: white plaster between a red timber frame. */
    if (!AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 10, feature,
                        s, 6, 2, 7, 17, 6, 13, BLOCK_WHITE_PLASTER) ||
        !AppendLocalBox(workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, 0, feature,
                        s, 7, 3, 8, 15, 5, 11, BLOCK_AIR)) return false;

    /* Central double-door opening and four regularly spaced lattice windows. */
    const int windowX[] = { 7, 10, 17, 20 };
    if (!AppendLocalBox(workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, 1, feature,
                        s, 12, 2, 7, 4, 5, 1, BLOCK_AIR)) return false;
    for (size_t i = 0; i < sizeof(windowX) / sizeof(windowX[0]); i++) {
        if (!AppendLocalBox(workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, 1, feature,
                            s, windowX[i], 4, 7, 2, 3, 1, BLOCK_AIR)) return false;
    }
    for (int side = 0; side < 2; side++) {
        for (int slot = 0; slot < 2; slot++) {
            int x = side ? 22 : 6;
            int z = slot ? 15 : 9;
            if (!AppendLocalBox(workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, 1,
                                feature, s, x, 4, z, 1, 3, 3, BLOCK_AIR) ||
                !AppendLocalBox(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 2,
                                feature, s, x, 4, z, 1, 3, 3,
                                BLOCK_WOOD_LATTICE)) return false;
        }
    }

    /* Six front and six rear columns create five bays; side columns complete
     * the peristyle and the wide overhanging eaves. */
    const int bayColumns[] = { 6, 9, 12, 16, 19, 22 };
    const int sideColumns[] = { 10, 13, 16 };
    for (size_t i = 0; i < sizeof(bayColumns) / sizeof(bayColumns[0]); i++) {
        for (int side = 0; side < 2; side++) {
            if (!AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 20,
                                feature, s, bayColumns[i], 2,
                                side ? 20 : 6, 1, 6, 1,
                                BLOCK_RED_LACQUER_WOOD)) return false;
        }
    }
    for (int side = 0; side < 2; side++) {
        for (size_t i = 0; i < sizeof(sideColumns) / sizeof(sideColumns[0]); i++) {
            if (!AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 20,
                                feature, s, side ? 23 : 5, 2, sideColumns[i],
                                1, 6, 1, BLOCK_RED_LACQUER_WOOD)) return false;
        }
    }

    /* Red architraves and a green painted plate form the transition from the
     * timber frame to the roof. Gold bracket tips repeat along each facade. */
    if (!AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 24, feature,
                        s, 5, 8, 6, 19, 1, 1, BLOCK_RED_LACQUER_WOOD) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 24, feature,
                        s, 5, 8, 20, 19, 1, 1, BLOCK_RED_LACQUER_WOOD) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 24, feature,
                        s, 5, 8, 6, 1, 1, 15, BLOCK_RED_LACQUER_WOOD) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 24, feature,
                        s, 23, 8, 6, 1, 1, 15, BLOCK_RED_LACQUER_WOOD) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 25, feature,
                        s, 4, 9, 6, 21, 1, 1, BLOCK_GREEN) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 25, feature,
                        s, 4, 9, 20, 21, 1, 1, BLOCK_GREEN)) return false;

    const int bracketX[] = { 6, 9, 12, 14, 16, 19, 22 };
    for (size_t i = 0; i < sizeof(bracketX) / sizeof(bracketX[0]); i++) {
        if (!AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 26,
                            feature, s, bracketX[i], 9, 5, 1, 1, 1,
                            BLOCK_YELLOW) ||
            !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 26,
                            feature, s, bracketX[i], 9, 21, 1, 1, 1,
                            BLOCK_YELLOW)) return false;
    }
    for (int side = 0; side < 2; side++) {
        for (size_t i = 0; i < sizeof(sideColumns) / sizeof(sideColumns[0]); i++) {
            if (!AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 26,
                                feature, s, side ? 24 : 4, 9,
                                sideColumns[i] - 1, 1, 1, 3,
                                BLOCK_YELLOW)) return false;
        }
    }

    /* Stone balustrade leaves the central stair opening clear. */
    const int railPosts[] = { 2, 5, 8, 20, 23, 26 };
    for (size_t i = 0; i < sizeof(railPosts) / sizeof(railPosts[0]); i++) {
        if (!AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 28,
                            feature, s, railPosts[i], 2, 2, 1, 2, 1,
                            BLOCK_DRESSED_STONE)) return false;
    }
    if (!AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 28, feature,
                        s, 2, 3, 2, 9, 1, 1, BLOCK_DRESSED_STONE) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 28, feature,
                        s, 18, 3, 2, 9, 1, 1, BLOCK_DRESSED_STONE) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 28, feature,
                        s, 2, 3, 2, 1, 1, 23, BLOCK_DRESSED_STONE) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 28, feature,
                        s, 26, 3, 2, 1, 1, 23, BLOCK_DRESSED_STONE) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 28, feature,
                        s, 2, 3, 24, 25, 1, 1, BLOCK_DRESSED_STONE)) return false;

    /* Two red lanterns hang from the front bracket line. */
    for (int side = 0; side < 2; side++) {
        int x = side ? 20 : 8;
        if (!AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 29,
                            feature, s, x, 6, 4, 1, 2, 1, BLOCK_RED) ||
            !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 29,
                            feature, s, x, 5, 4, 1, 1, 1,
                            BLOCK_YELLOW)) return false;
    }

    /* A seven-course stepped hip roof. Each course is shallower near the eaves
     * and steeper toward the ridge, producing the rhythmic concave silhouette
     * of a Chinese roof in voxel resolution. */
    const int roofX[] = { 2, 3, 5, 7, 9, 11 };
    const int roofZ[] = { 2, 3, 4, 6, 8, 10 };
    const int roofW[] = { 25, 23, 19, 15, 11, 7 };
    const int roofD[] = { 23, 21, 19, 15, 11, 7 };
    for (int level = 0; level < 6; level++) {
        if (!AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE,
                            40 + level, feature, s, roofX[level],
                            10 + level, roofZ[level], roofW[level], 1,
                            roofD[level], BLOCK_BLUE_ROOF_TILE)) return false;
    }

    /* Upturned corner horns: one outward step at the eave and two more rising
     * diagonally, capped with the darker blue-brick ridge material. */
    const int cornersX[] = { 2, 26, 2, 26 };
    const int cornersZ[] = { 2, 24, 24, 2 };
    const int cornerDX[] = { -1, 1, -1, 1 };
    const int cornerDZ[] = { -1, 1, 1, -1 };
    for (int i = 0; i < 4; i++) {
        if (!AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 50,
                            feature, s, cornersX[i] + cornerDX[i], 10,
                            cornersZ[i] + cornerDZ[i], 1, 1, 1,
                            BLOCK_BLUE_ROOF_TILE) ||
            !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 51,
                            feature, s, cornersX[i] + 2 * cornerDX[i], 11,
                            cornersZ[i] + 2 * cornerDZ[i], 1, 1, 1,
                            BLOCK_BLUE_ROOF_TILE) ||
            !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 52,
                            feature, s, cornersX[i] + 2 * cornerDX[i], 12,
                            cornersZ[i] + 2 * cornerDZ[i], 1, 1, 1,
                            BLOCK_BLUE_BRICK)) return false;
    }

    if (!AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 60, feature,
                        s, 10, 16, 13, 9, 1, 1, BLOCK_BLUE_BRICK) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 61, feature,
                        s, 9, 16, 13, 1, 2, 1, BLOCK_BLUE_BRICK) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 61, feature,
                        s, 19, 16, 13, 1, 2, 1, BLOCK_BLUE_BRICK) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 62, feature,
                        s, 9, 18, 13, 1, 1, 1, BLOCK_YELLOW) ||
        !AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, 62, feature,
                        s, 19, 18, 13, 1, 1, 1, BLOCK_YELLOW)) return false;

    /* Closed lower door leaves, open lattice transoms, and a green lintel. */
    if (!AppendLocalBox(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 0, feature,
                        s, 7, 2, 8, 15, 1, 11, BLOCK_WOOD) ||
        !AppendLocalBox(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 1, feature,
                        s, 12, 2, 7, 4, 2, 1, BLOCK_RED_LACQUER_WOOD) ||
        !AppendLocalBox(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 1, feature,
                        s, 12, 4, 7, 4, 3, 1, BLOCK_WOOD_LATTICE) ||
        !AppendLocalBox(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 2, feature,
                        s, 11, 7, 7, 6, 1, 1, BLOCK_GREEN)) return false;
    for (size_t i = 0; i < sizeof(windowX) / sizeof(windowX[0]); i++) {
        if (!AppendLocalBox(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 2,
                            feature, s, windowX[i], 4, 7, 2, 3, 1,
                            BLOCK_WOOD_LATTICE)) return false;
    }
    /* A few oversized pixels imply the gold door studs at this scale. */
    if (!AppendLocalBox(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 3, feature,
                        s, 13, 3, 7, 1, 1, 1, BLOCK_YELLOW) ||
        !AppendLocalBox(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 3, feature,
                        s, 14, 3, 7, 1, 1, 1, BLOCK_YELLOW)) return false;

    return true;
}

bool CgaExpandFeature(CgaWorkspace *workspace, CgaFeature feature)
{
    switch (feature.grammarVersion) {
        case CGA_HOUSE_GRAMMAR_VERSION: return CgaExpandHouse(workspace, feature);
        case CGA_MINESHAFT_GRAMMAR_VERSION: return ExpandMineshaft(workspace, feature);
        case CGA_DUNGEON_GRAMMAR_VERSION: return ExpandDungeon(workspace, feature);
        case CGA_DESERT_TEMPLE_GRAMMAR_VERSION: return ExpandDesertTemple(workspace, feature);
        case CGA_CLASSICAL_HALL_GRAMMAR_VERSION: return ExpandClassicalHall(workspace, feature);
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

    const int hallSpacing = 160;
    const int hallHalfExtent = 16;
    int minAnchorX = FloorDivInt(startX - hallHalfExtent, hallSpacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + hallHalfExtent, hallSpacing);
    int minAnchorZ = FloorDivInt(startZ - hallHalfExtent, hallSpacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + hallHalfExtent, hallSpacing);
    for (int ax = minAnchorX; ax <= maxAnchorX; ax++) {
        for (int az = minAnchorZ; az <= maxAnchorZ; az++) {
            int x = ax * hallSpacing;
            int z = az * hallSpacing;
            Biome biome = BiomeAt(x, z);
            if ((biome != BIOME_PLAINS && biome != BIOME_FOREST) ||
                HomeSurfaceHashAt(x, z, 1543u) % 100u >= 42u ||
                TerrainHeight(x, z, mode) < 4) continue;
            int base = TerrainHeight(x, z, mode);
            CgaOrientation orientation =
                (CgaOrientation)(HomeSurfaceHashAt(x, z, 1549u) % 4u);
            int width = (orientation & 1) ? 27 : 29;
            int depth = (orientation & 1) ? 29 : 27;
            CgaScope scope = { x - width / 2, base, z - depth / 2,
                               width, 19, depth, orientation };
            if (scope.minX >= startX + CHUNK_SIZE ||
                scope.minX + scope.sizeX <= startX ||
                scope.minZ >= startZ + CHUNK_SIZE ||
                scope.minZ + scope.sizeZ <= startZ) continue;
            if (!StructureTerrainSuitable(scope, base, mode, 2)) continue;
            if (!AddFeature(workspace, CGA_CLASSICAL_HALL_GRAMMAR_VERSION, seed,
                            ax, az, scope)) return false;
        }
    }

    const int houseSpacing = 64;
    const int houseHalfExtent = 9;
    minAnchorX = FloorDivInt(startX - houseHalfExtent, houseSpacing);
    maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + houseHalfExtent, houseSpacing);
    minAnchorZ = FloorDivInt(startZ - houseHalfExtent, houseSpacing);
    maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + houseHalfExtent, houseSpacing);
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
            bool hallReserved = false;
            for (size_t i = 0; i < workspace->featureCount; i++) {
                if (workspace->features[i].grammarVersion ==
                        CGA_CLASSICAL_HALL_GRAMMAR_VERSION &&
                    ScopeOverlapsXZ(scope, workspace->features[i].scope)) {
                    hallReserved = true;
                    break;
                }
            }
            if (hallReserved) continue;
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

size_t CgaTestPlanStructureFeatures(CgaFeature *out, size_t capacity,
                                    int cx, int cz, TerrainMode mode,
                                    const CgaContext *context)
{
    CgaWorkspace workspace;
    CgaWorkspaceReset(&workspace);
    if (!CgaPlanStructureFeatures(&workspace, cx, cz, mode, context) || !out) return 0;
    size_t count = workspace.featureCount < capacity ? workspace.featureCount : capacity;
    memcpy(out, workspace.features, count * sizeof(*out));
    return count;
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
