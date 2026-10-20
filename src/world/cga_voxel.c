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

/* A courtyard complex sits on a terrace.  Use the highest point of the
 * footprint plus a surrounding apron as the terrace level so the complex is
 * not sunk into a hollow whose rim hides everything but the main roof.  A
 * site may fall by up to ten blocks across that area; the deep foundation
 * covers the low corners. */
static bool ComplexTerrainBase(CgaScope scope, TerrainMode mode, int *outBase)
{
    const int margin = 12;
    int minHeight = INT_MAX;
    int maxHeight = INT_MIN;
    for (int x = scope.minX - margin; x < scope.minX + scope.sizeX + margin; x++) {
        for (int z = scope.minZ - margin; z < scope.minZ + scope.sizeZ + margin; z++) {
            int height = TerrainHeight(x, z, mode);
            if (height < minHeight) minHeight = height;
            if (height > maxHeight) maxHeight = height;
        }
    }
    if (maxHeight < 4 || maxHeight - minHeight > 10) return false;
    if (outBase) *outBase = maxHeight;
    return true;
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
 * A Chinese classical courtyard complex.  The layout is axial: a gatehouse
 * on the south wall, a paved forecourt with a ceremonial path, two side
 * halls facing the court, and a raised five-bay main hall at the north end.
 * The whole compound sits on a stone terrace so it can occupy gently
 * sloping ground without floating walls.
 *
 * Local coordinates are x from west to east, z from north to south, and y
 * upwards from the feature scope base.  The gate is at the south (high z).
 */
#define CGA_COMPLEX_INNER_WIDTH 73
#define CGA_COMPLEX_YARD_DEPTH 61
#define CGA_COMPLEX_APRON 20
#define CGA_COMPLEX_INNER_DEPTH (CGA_COMPLEX_YARD_DEPTH + CGA_COMPLEX_APRON)
#define CGA_COMPLEX_MARGIN 10
#define CGA_COMPLEX_WIDTH (CGA_COMPLEX_INNER_WIDTH + 2 * CGA_COMPLEX_MARGIN)
#define CGA_COMPLEX_DEPTH (CGA_COMPLEX_INNER_DEPTH + 2 * CGA_COMPLEX_MARGIN)
#define CGA_COMPLEX_HEIGHT 28

static bool ComplexFill(CgaWorkspace *workspace, CgaFeature feature,
                        CgaScope s, int x, int y, int z,
                        int width, int height, int depth,
                        BlockType material, int priority)
{
    return AppendLocalBox(workspace, CGA_OP_FILL, CGA_STAGE_BASE, priority,
                          feature, s, x + CGA_COMPLEX_MARGIN, y,
                          z + CGA_COMPLEX_MARGIN, width, height, depth,
                          material);
}

static bool ComplexCarve(CgaWorkspace *workspace, CgaFeature feature,
                         CgaScope s, int x, int y, int z,
                         int width, int height, int depth,
                         BlockType material, int priority)
{
    return AppendLocalBox(workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, priority,
                          feature, s, x + CGA_COMPLEX_MARGIN, y,
                          z + CGA_COMPLEX_MARGIN, width, height, depth,
                          material);
}

static bool ComplexPaint(CgaWorkspace *workspace, CgaFeature feature,
                         CgaScope s, int x, int y, int z,
                         int width, int height, int depth,
                         BlockType material, int priority)
{
    return AppendLocalBox(workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, priority,
                          feature, s, x + CGA_COMPLEX_MARGIN, y,
                          z + CGA_COMPLEX_MARGIN, width, height, depth,
                          material);
}

static bool AppendComplexWall(CgaWorkspace *workspace, CgaFeature feature,
                              CgaScope s, int x, int z, int width, int depth,
                              int priority)
{
    return ComplexFill(workspace, feature, s, x, 2, z, width, 1, depth,
                       BLOCK_DRESSED_STONE, priority) &&
           ComplexFill(workspace, feature, s, x, 3, z, width, 3, depth,
                       BLOCK_RED_LACQUER_WOOD, priority + 1) &&
           ComplexFill(workspace, feature, s, x - 1, 6, z - 1,
                       width + 2, 1, depth + 2, BLOCK_BLUE_ROOF_TILE,
                       priority + 2) &&
           ComplexFill(workspace, feature, s, x, 7, z, width, 1, depth,
                       BLOCK_BLUE_BRICK, priority + 3);
}

static int ComplexRoofInset(int maxInset, int level, int levels)
{
    if (levels <= 1) return 0;
    int denom = levels - 1;
    return (maxInset * level * level) / (denom * denom);
}

static bool AppendComplexHipRoof(CgaWorkspace *workspace, CgaFeature feature,
                                 CgaScope s, int x, int z, int width,
                                 int depth, int eaveY, int levels,
                                 int priority, int cornerReach)
{
    if (width <= depth || depth < 3 || levels < 2 ||
        cornerReach < 0 || cornerReach > 4) return false;
    int maxInset = (depth - 1) / 2;
    for (int i = 0; i < levels; i++) {
        int inset = ComplexRoofInset(maxInset, i, levels);
        int w = width - 2 * inset;
        int d = depth - 2 * inset;
        if (d < 1) d = 1;
        if (!ComplexFill(workspace, feature, s, x + inset, eaveY + i,
                         z + inset, w, 1, d, BLOCK_BLUE_ROOF_TILE,
                         priority + i)) return false;
    }
    for (int i = 0; i < levels; i++) {
        int inset = ComplexRoofInset(maxInset, i, levels);
        int w = width - 2 * inset;
        int d = depth - 2 * inset;
        if (d < 1) d = 1;
        int y = eaveY + i + 1;
        if (!ComplexFill(workspace, feature, s, x + inset, y, z + inset,
                         1, 1, 1, BLOCK_BLUE_BRICK, priority + 20 + i) ||
            !ComplexFill(workspace, feature, s, x + inset + w - 1, y,
                         z + inset, 1, 1, 1, BLOCK_BLUE_BRICK,
                         priority + 20 + i) ||
            !ComplexFill(workspace, feature, s, x + inset, y,
                         z + inset + d - 1, 1, 1, 1, BLOCK_BLUE_BRICK,
                         priority + 20 + i) ||
            !ComplexFill(workspace, feature, s, x + inset + w - 1, y,
                         z + inset + d - 1, 1, 1, 1, BLOCK_BLUE_BRICK,
                         priority + 20 + i)) return false;
    }
    const int cornerX[4] = { -1, 1, -1, 1 };
    const int cornerZ[4] = { -1, -1, 1, 1 };
    for (int c = 0; c < 4; c++) {
        int cx = cornerX[c] < 0 ? x : x + width - 1;
        int cz = cornerZ[c] < 0 ? z : z + depth - 1;
        for (int step = 1; step <= cornerReach; step++) {
            if (!ComplexFill(workspace, feature, s,
                             cx + cornerX[c] * step, eaveY + step,
                             cz + cornerZ[c] * step, 1, 1, 1,
                             BLOCK_BLUE_ROOF_TILE, priority + 40 + step))
                return false;
        }
        if (!ComplexFill(workspace, feature, s,
                         cx + cornerX[c] * cornerReach,
                         eaveY + cornerReach + 1,
                         cz + cornerZ[c] * cornerReach, 1, 1, 1,
                         BLOCK_YELLOW, priority + 45 + c)) return false;
    }
    /* A descending group of ridge beasts along each hip.  The base sits on
     * the corner ridge line and the gold head reads as one small figure. */
    for (int c = 0; c < 4; c++) {
        for (int i = levels - 2; i >= 1; i -= 2) {
            int inset = ComplexRoofInset(maxInset, i, levels);
            int bx = cornerX[c] < 0 ? x + inset : x + width - 1 - inset;
            int bz = cornerZ[c] < 0 ? z + inset : z + depth - 1 - inset;
            int by = eaveY + i + 2;
            if (!ComplexFill(workspace, feature, s, bx, by, bz,
                             1, 1, 1, BLOCK_BLUE_BRICK, priority + 100 + i) ||
                !ComplexFill(workspace, feature, s, bx, by + 1, bz,
                             1, 1, 1, BLOCK_YELLOW, priority + 100 + i))
                return false;
        }
    }

    /* Tile ribs: paint alternating columns down the four slope faces so the
     * stepped roof reads as parallel tile courses rather than flat boxes. */
    for (int i = 0; i < levels; i++) {
        int inset = ComplexRoofInset(maxInset, i, levels);
        int w = width - 2 * inset;
        int d = depth - 2 * inset;
        if (d < 1) d = 1;
        int fz = z + inset + d - 1;
        int bz = z + inset;
        for (int tx = x + inset + 2; tx < x + inset + w - 2; tx += 3) {
            if (!ComplexPaint(workspace, feature, s, tx, eaveY + i, fz,
                              1, 1, 1, BLOCK_BLUE_BRICK, priority + 80 + i) ||
                !ComplexPaint(workspace, feature, s, tx, eaveY + i, bz,
                              1, 1, 1, BLOCK_BLUE_BRICK, priority + 80 + i))
                return false;
        }
        for (int tz = z + inset + 2; tz < z + inset + d - 2; tz += 3) {
            if (!ComplexPaint(workspace, feature, s, x + inset, eaveY + i,
                              tz, 1, 1, 1, BLOCK_BLUE_BRICK,
                              priority + 80 + i) ||
                !ComplexPaint(workspace, feature, s, x + inset + w - 1,
                              eaveY + i, tz, 1, 1, 1, BLOCK_BLUE_BRICK,
                              priority + 80 + i)) return false;
        }
    }

    /* Eave layers: an outward drip tile, exposed rafter ends one block under
     * the fascia, and a raised tile lip on the eave course. */
    if (!ComplexFill(workspace, feature, s, x, eaveY, z + depth,
                     width, 1, 1, BLOCK_BLUE_BRICK, priority + 90) ||
        !ComplexFill(workspace, feature, s, x, eaveY, z - 1,
                     width, 1, 1, BLOCK_BLUE_BRICK, priority + 90) ||
        !ComplexFill(workspace, feature, s, x - 1, eaveY, z,
                     1, 1, depth, BLOCK_BLUE_BRICK, priority + 90) ||
        !ComplexFill(workspace, feature, s, x + width, eaveY, z,
                     1, 1, depth, BLOCK_BLUE_BRICK, priority + 90) ||
        !ComplexFill(workspace, feature, s, x, eaveY + 1, z + depth - 1,
                     width, 1, 1, BLOCK_BLUE_BRICK, priority + 92) ||
        !ComplexFill(workspace, feature, s, x, eaveY + 1, z,
                     width, 1, 1, BLOCK_BLUE_BRICK, priority + 92))
        return false;
    for (int e = 0; e < width; e += 2) {
        if (!ComplexFill(workspace, feature, s, x + e, eaveY - 2,
                         z + depth - 1, 1, 1, 1, BLOCK_WOOD,
                         priority + 91) ||
            !ComplexFill(workspace, feature, s, x + e, eaveY - 2, z,
                         1, 1, 1, BLOCK_WOOD, priority + 91))
            return false;
    }
    for (int e = 0; e < depth; e += 2) {
        if (!ComplexFill(workspace, feature, s, x, eaveY - 2, z + e,
                         1, 1, 1, BLOCK_WOOD, priority + 91) ||
            !ComplexFill(workspace, feature, s, x + width - 1, eaveY - 2,
                         z + e, 1, 1, 1, BLOCK_WOOD, priority + 91))
            return false;
    }

    int inset = maxInset;
    int w = width - 2 * inset;
    int d = depth - 2 * inset;
    if (d < 1) d = 1;
    int topY = eaveY + levels;
    if (!ComplexFill(workspace, feature, s, x + inset, topY, z + inset,
                     w, 1, 1, BLOCK_BLUE_BRICK, priority + 60) ||
        !ComplexFill(workspace, feature, s, x + inset, topY + 1, z + inset,
                     1, 2, 1, BLOCK_BLUE_BRICK, priority + 61) ||
        !ComplexFill(workspace, feature, s, x + inset + w - 1, topY + 1,
                     z + inset, 1, 2, 1, BLOCK_BLUE_BRICK, priority + 62) ||
        !ComplexFill(workspace, feature, s, x + inset, topY + 2, z + inset,
                     1, 1, 1, BLOCK_YELLOW, priority + 63) ||
        !ComplexFill(workspace, feature, s, x + inset + w - 1, topY + 2,
                     z + inset, 1, 1, 1, BLOCK_YELLOW, priority + 64) ||
        !ComplexFill(workspace, feature, s, x + inset, topY + 3, z + inset,
                     1, 1, 1, BLOCK_YELLOW, priority + 65) ||
        !ComplexFill(workspace, feature, s, x + inset + w - 1, topY + 3,
                     z + inset, 1, 1, 1, BLOCK_YELLOW, priority + 66))
        return false;
    if (!ComplexFill(workspace, feature, s, x, eaveY - 1, z,
                     width, 1, 1, BLOCK_GREEN, priority + 70) ||
        !ComplexFill(workspace, feature, s, x, eaveY - 1, z + depth - 1,
                     width, 1, 1, BLOCK_GREEN, priority + 71) ||
        !ComplexFill(workspace, feature, s, x, eaveY - 1, z,
                     1, 1, depth, BLOCK_GREEN, priority + 72) ||
        !ComplexFill(workspace, feature, s, x + width - 1, eaveY - 1, z,
                     1, 1, depth, BLOCK_GREEN, priority + 73)) return false;
    return true;
}

static bool AppendComplexGableRoof(CgaWorkspace *workspace, CgaFeature feature,
                                   CgaScope s, int x, int z, int width,
                                   int depth, int eaveY, int levels,
                                   int priority)
{
    if (width < 3 || depth < 3 || levels < 2) return false;
    int maxInset = (width - 1) / 2;
    for (int i = 0; i < levels; i++) {
        int inset = ComplexRoofInset(maxInset, i, levels);
        int w = width - 2 * inset;
        if (!ComplexFill(workspace, feature, s, x + inset, eaveY + i, z,
                         w, 1, depth, BLOCK_BLUE_ROOF_TILE, priority + i))
            return false;
    }
    /* Tile ribs and layered eaves on the courtyard-facing slopes. */
    for (int i = 0; i < levels; i++) {
        int inset = ComplexRoofInset(maxInset, i, levels);
        int w = width - 2 * inset;
        for (int tz = z + 2; tz < z + depth - 2; tz += 3) {
            if (!ComplexPaint(workspace, feature, s, x + inset,
                              eaveY + i, tz, 1, 1, 1, BLOCK_BLUE_BRICK,
                              priority + 50 + i) ||
                !ComplexPaint(workspace, feature, s, x + inset + w - 1,
                              eaveY + i, tz, 1, 1, 1, BLOCK_BLUE_BRICK,
                              priority + 50 + i)) return false;
        }
    }
    if (!ComplexFill(workspace, feature, s, x - 1, eaveY, z,
                     1, 1, depth, BLOCK_BLUE_BRICK, priority + 60) ||
        !ComplexFill(workspace, feature, s, x + width, eaveY, z,
                     1, 1, depth, BLOCK_BLUE_BRICK, priority + 60) ||
        !ComplexFill(workspace, feature, s, x - 1, eaveY + 1, z,
                     1, 1, depth, BLOCK_BLUE_BRICK, priority + 61) ||
        !ComplexFill(workspace, feature, s, x + width, eaveY + 1, z,
                     1, 1, depth, BLOCK_BLUE_BRICK, priority + 61))
        return false;
    for (int tz = z; tz < z + depth; tz += 2) {
        if (!ComplexFill(workspace, feature, s, x, eaveY - 2, tz,
                         1, 1, 1, BLOCK_WOOD, priority + 62) ||
            !ComplexFill(workspace, feature, s, x + width - 1, eaveY - 2,
                         tz, 1, 1, 1, BLOCK_WOOD, priority + 62))
            return false;
    }
    /* Gable-end ridge line (bargeboard) along both slopes. */
    for (int i = 0; i < levels; i++) {
        int inset = ComplexRoofInset(maxInset, i, levels);
        int w = width - 2 * inset;
        int y = eaveY + i + 1;
        if (!ComplexFill(workspace, feature, s, x + inset, y, z,
                         1, 1, 1, BLOCK_BLUE_BRICK, priority + 70 + i) ||
            !ComplexFill(workspace, feature, s, x + inset + w - 1, y, z,
                         1, 1, 1, BLOCK_BLUE_BRICK, priority + 70 + i) ||
            !ComplexFill(workspace, feature, s, x + inset, y, z + depth - 1,
                         1, 1, 1, BLOCK_BLUE_BRICK, priority + 70 + i) ||
            !ComplexFill(workspace, feature, s, x + inset + w - 1, y,
                         z + depth - 1, 1, 1, 1, BLOCK_BLUE_BRICK,
                         priority + 70 + i)) return false;
    }
    int topY = eaveY + levels;
    if (!ComplexFill(workspace, feature, s, x + maxInset, topY, z,
                     1, 1, depth, BLOCK_BLUE_BRICK, priority + 20))
        return false;
    for (int bz = z + 2; bz <= z + depth - 3; bz += 4) {
        if (!ComplexFill(workspace, feature, s, x + maxInset, topY + 1, bz,
                         1, 1, 1, BLOCK_BLUE_BRICK, priority + 80) ||
            !ComplexFill(workspace, feature, s, x + maxInset, topY + 2, bz,
                         1, 1, 1, BLOCK_YELLOW, priority + 81))
            return false;
    }
    if (depth > 4 &&
        (!ComplexFill(workspace, feature, s, x + maxInset, topY + 1,
                      z + 1, 1, 1, 2, BLOCK_YELLOW, priority + 21) ||
         !ComplexFill(workspace, feature, s, x + maxInset, topY + 1,
                      z + depth - 3, 1, 1, 2, BLOCK_YELLOW,
                      priority + 21))) return false;
    for (int i = 0; i < levels - 1; i++) {
        int inset = ComplexRoofInset(maxInset, i, levels);
        int w = width - 2 * inset;
        if (!ComplexPaint(workspace, feature, s, x + inset, eaveY + i, z,
                          w, 1, 1, BLOCK_WHITE_PLASTER, priority + 30 + i) ||
            !ComplexPaint(workspace, feature, s, x + inset, eaveY + i,
                          z + depth - 1, w, 1, 1, BLOCK_WHITE_PLASTER,
                          priority + 30 + i)) return false;
    }
    if (!ComplexFill(workspace, feature, s, x, eaveY - 1, z,
                     1, 1, depth, BLOCK_GREEN, priority + 40) ||
        !ComplexFill(workspace, feature, s, x + width - 1, eaveY - 1, z,
                     1, 1, depth, BLOCK_GREEN, priority + 41) ||
        !ComplexFill(workspace, feature, s, x, eaveY - 1, z,
                     width, 1, 1, BLOCK_GREEN, priority + 42) ||
        !ComplexFill(workspace, feature, s, x, eaveY - 1, z + depth - 1,
                     width, 1, 1, BLOCK_GREEN, priority + 43)) return false;
    return true;
}

static bool AppendComplexPine(CgaWorkspace *workspace, CgaFeature feature,
                              CgaScope s, int x, int z, int height)
{
    if (height < 4) return false;
    if (!ComplexFill(workspace, feature, s, x, 2, z, 1, height, 1,
                     BLOCK_WOOD, 70) ||
        !ComplexFill(workspace, feature, s, x - 2, 2 + height - 3, z - 1,
                     5, 2, 3, BLOCK_LEAVES, 71) ||
        !ComplexFill(workspace, feature, s, x - 1, 2 + height - 1, z,
                     3, 1, 3, BLOCK_LEAVES, 72) ||
        !ComplexFill(workspace, feature, s, x, 2 + height, z,
                     1, 1, 1, BLOCK_LEAVES, 73)) return false;
    return true;
}

static bool AppendComplexLion(CgaWorkspace *workspace, CgaFeature feature,
                              CgaScope s, int x, int z)
{
    return ComplexFill(workspace, feature, s, x, 2, z, 2, 1, 2,
                       BLOCK_DRESSED_STONE, 60) &&
           ComplexFill(workspace, feature, s, x, 3, z, 2, 2, 2,
                       BLOCK_DRESSED_STONE, 61) &&
           ComplexFill(workspace, feature, s, x, 5, z, 1, 2, 1,
                       BLOCK_DRESSED_STONE, 62) &&
           ComplexPaint(workspace, feature, s, x, 5, z + 1, 1, 1, 1,
                        BLOCK_BLUE_BRICK, 0);
}

static bool ExpandClassicalComplex(CgaWorkspace *workspace, CgaFeature feature)
{
    CgaScope s = feature.scope;
    int width = (s.orientation & 1) ? s.sizeZ : s.sizeX;
    int depth = (s.orientation & 1) ? s.sizeX : s.sizeZ;
    if (!workspace ||
        feature.grammarVersion != CGA_CLASSICAL_COMPLEX_GRAMMAR_VERSION ||
        !CgaScopeValid(s) || width < CGA_COMPLEX_WIDTH ||
        depth < CGA_COMPLEX_DEPTH || s.sizeY < CGA_COMPLEX_HEIGHT)
        return false;
    if (!ReserveOps(workspace, 1200)) return false;

#define CF(x, y, z, w, h, d, m, p) do { \
    if (!ComplexFill(workspace, feature, s, (x), (y), (z), (w), (h), (d), \
                     (m), (p))) return false; \
} while (0)
#define CC(x, y, z, w, h, d, m, p) do { \
    if (!ComplexCarve(workspace, feature, s, (x), (y), (z), (w), (h), (d), \
                      (m), (p))) return false; \
} while (0)
#define CP(x, y, z, w, h, d, m, p) do { \
    if (!ComplexPaint(workspace, feature, s, (x), (y), (z), (w), (h), (d), \
                      (m), (p))) return false; \
} while (0)

    /* Remove trees and loose terrain above the courtyard, then fill the
     * terrace down to the lowest corner of the site. */
    if (!AppendLocalBox(workspace, CGA_OP_CARVE, CGA_STAGE_BASE, 0, feature,
                        s, 0, 2, 0, CGA_COMPLEX_WIDTH, CGA_COMPLEX_HEIGHT,
                        CGA_COMPLEX_DEPTH, BLOCK_AIR)) return false;
    CF(0, -12, 0, CGA_COMPLEX_INNER_WIDTH, 14, CGA_COMPLEX_INNER_DEPTH,
       BLOCK_STONE_BRICKS, 1);
    CF(2, 2, 2, CGA_COMPLEX_INNER_WIDTH - 4, 1, CGA_COMPLEX_INNER_DEPTH - 4,
       BLOCK_STONE_BRICKS, 2);

    /* Ceremonial path and the cross path between the side halls. */
    CP(34, 2, 31, 5, 1, 49, BLOCK_DRESSED_STONE, 0);
    CP(21, 2, 40, 30, 1, 3, BLOCK_DRESSED_STONE, 0);

    /* Outer wall: one gate opening in the south wall. */
    if (!AppendComplexWall(workspace, feature, s, 2, 2,
                           CGA_COMPLEX_INNER_WIDTH - 4, 1, 4) ||
        !AppendComplexWall(workspace, feature, s, 2, 2, 1,
                           CGA_COMPLEX_YARD_DEPTH - 4, 4) ||
        !AppendComplexWall(workspace, feature, s, CGA_COMPLEX_INNER_WIDTH - 3, 2, 1,
                           CGA_COMPLEX_YARD_DEPTH - 4, 4) ||
        !AppendComplexWall(workspace, feature, s, 2, CGA_COMPLEX_YARD_DEPTH - 3,
                           25, 1, 4) ||
        !AppendComplexWall(workspace, feature, s, 46, CGA_COMPLEX_YARD_DEPTH - 3,
                           25, 1, 4)) return false;

    /* Gatehouse: a three-bay gate with an open central passage. */
    CF(27, 3, 49, 19, 6, 10, BLOCK_RED_LACQUER_WOOD, 10);
    CC(33, 3, 49, 7, 6, 10, BLOCK_AIR, 0);
    CC(28, 4, 58, 4, 2, 1, BLOCK_AIR, 1);
    CC(41, 4, 58, 4, 2, 1, BLOCK_AIR, 1);
    CP(28, 4, 58, 4, 2, 1, BLOCK_WOOD_LATTICE, 0);
    CP(41, 4, 58, 4, 2, 1, BLOCK_WOOD_LATTICE, 0);
    CF(27, 3, 59, 1, 6, 1, BLOCK_RED_LACQUER_WOOD, 11);
    CF(33, 3, 59, 1, 6, 1, BLOCK_RED_LACQUER_WOOD, 11);
    CF(39, 3, 59, 1, 6, 1, BLOCK_RED_LACQUER_WOOD, 11);
    CF(45, 3, 59, 1, 6, 1, BLOCK_RED_LACQUER_WOOD, 11);
    CF(27, 3, 51, 1, 6, 1, BLOCK_RED_LACQUER_WOOD, 11);
    CF(45, 3, 51, 1, 6, 1, BLOCK_RED_LACQUER_WOOD, 11);
    CF(26, 8, 59, 21, 1, 1, BLOCK_RED_LACQUER_WOOD, 20);
    CF(25, 9, 59, 23, 1, 1, BLOCK_GREEN, 21);
    CF(31, 8, 59, 9, 2, 1, BLOCK_BLACK, 22);
    CP(32, 9, 59, 1, 1, 1, BLOCK_YELLOW, 1);
    CP(34, 9, 59, 1, 1, 1, BLOCK_YELLOW, 1);
    CP(36, 9, 59, 1, 1, 1, BLOCK_YELLOW, 1);
    CF(32, 3, 57, 1, 5, 1, BLOCK_RED_LACQUER_WOOD, 22);
    CF(40, 3, 57, 1, 5, 1, BLOCK_RED_LACQUER_WOOD, 22);
    CP(32, 4, 57, 1, 1, 1, BLOCK_YELLOW, 5);
    CP(32, 6, 57, 1, 1, 1, BLOCK_YELLOW, 5);
    CP(40, 4, 57, 1, 1, 1, BLOCK_YELLOW, 5);
    CP(40, 6, 57, 1, 1, 1, BLOCK_YELLOW, 5);
    CF(33, 2, 58, 7, 1, 1, BLOCK_DRESSED_STONE, 23);
    if (!AppendComplexHipRoof(workspace, feature, s, 25, 46, 23, 14,
                              10, 4, 30, 1)) return false;

    /* Main hall: raised terrace, five-bay timber frame, deep hip roof. */
    CF(14, 3, 3, 45, 3, 28, BLOCK_DRESSED_STONE, 10);
    CF(30, 2, 30, 13, 1, 2, BLOCK_DRESSED_STONE, 9);
    CF(31, 3, 30, 11, 1, 2, BLOCK_DRESSED_STONE, 9);
    CF(32, 4, 30, 9, 1, 2, BLOCK_DRESSED_STONE, 9);
    CF(33, 5, 30, 7, 1, 2, BLOCK_DRESSED_STONE, 9);
    CF(18, 6, 6, 37, 12, 16, BLOCK_WHITE_PLASTER, 12);
    CC(19, 7, 7, 35, 4, 14, BLOCK_AIR, 0);

    /* Door and four lattice windows on the south facade. */
    CC(33, 6, 21, 5, 5, 1, BLOCK_AIR, 1);
    CP(33, 6, 21, 2, 4, 1, BLOCK_RED_LACQUER_WOOD, 0);
    CP(36, 6, 21, 2, 4, 1, BLOCK_RED_LACQUER_WOOD, 0);
    CP(33, 7, 21, 1, 1, 1, BLOCK_YELLOW, 1);
    CP(36, 9, 21, 1, 1, 1, BLOCK_YELLOW, 1);
    CP(33, 10, 21, 5, 1, 1, BLOCK_WOOD_LATTICE, 2);
    const int windowX[] = { 20, 27, 41, 47 };
    for (size_t i = 0; i < sizeof(windowX) / sizeof(windowX[0]); i++) {
        CC(windowX[i], 7, 21, 4, 3, 1, BLOCK_AIR, 1);
        CP(windowX[i], 7, 21, 4, 3, 1, BLOCK_WOOD_LATTICE, 0);
    }
    /* Side windows and rear windows make the hall readable from the court. */
    CC(18, 8, 9, 1, 3, 4, BLOCK_AIR, 1);
    CC(18, 8, 15, 1, 3, 4, BLOCK_AIR, 1);
    CC(54, 8, 9, 1, 3, 4, BLOCK_AIR, 1);
    CC(54, 8, 15, 1, 3, 4, BLOCK_AIR, 1);
    CP(18, 8, 9, 1, 3, 4, BLOCK_WOOD_LATTICE, 0);
    CP(18, 8, 15, 1, 3, 4, BLOCK_WOOD_LATTICE, 0);
    CP(54, 8, 9, 1, 3, 4, BLOCK_WOOD_LATTICE, 0);
    CP(54, 8, 15, 1, 3, 4, BLOCK_WOOD_LATTICE, 0);
    CC(24, 8, 6, 4, 3, 1, BLOCK_AIR, 1);
    CC(45, 8, 6, 4, 3, 1, BLOCK_AIR, 1);
    CP(24, 8, 6, 4, 3, 1, BLOCK_WOOD_LATTICE, 0);
    CP(45, 8, 6, 4, 3, 1, BLOCK_WOOD_LATTICE, 0);

    /* Upper-storey windows, side lights and rear lights. */
    const int upperWindowX[] = { 20, 27, 41, 47 };
    for (size_t i = 0; i < sizeof(upperWindowX) / sizeof(upperWindowX[0]); i++) {
        CC(upperWindowX[i], 14, 21, 4, 3, 1, BLOCK_AIR, 1);
        CP(upperWindowX[i], 14, 21, 4, 3, 1, BLOCK_WOOD_LATTICE, 0);
    }
    CC(18, 14, 9, 1, 3, 4, BLOCK_AIR, 1);
    CC(18, 14, 15, 1, 3, 4, BLOCK_AIR, 1);
    CC(54, 14, 9, 1, 3, 4, BLOCK_AIR, 1);
    CC(54, 14, 15, 1, 3, 4, BLOCK_AIR, 1);
    CP(18, 14, 9, 1, 3, 4, BLOCK_WOOD_LATTICE, 0);
    CP(18, 14, 15, 1, 3, 4, BLOCK_WOOD_LATTICE, 0);
    CP(54, 14, 9, 1, 3, 4, BLOCK_WOOD_LATTICE, 0);
    CP(54, 14, 15, 1, 3, 4, BLOCK_WOOD_LATTICE, 0);
    CC(24, 14, 6, 4, 3, 1, BLOCK_AIR, 1);
    CC(45, 14, 6, 4, 3, 1, BLOCK_AIR, 1);
    CP(24, 14, 6, 4, 3, 1, BLOCK_WOOD_LATTICE, 0);
    CP(45, 14, 6, 4, 3, 1, BLOCK_WOOD_LATTICE, 0);

    /* Six front columns, side columns, and rear columns. */
    const int frontColumns[] = { 18, 25, 32, 39, 46, 53 };
    for (size_t i = 0; i < sizeof(frontColumns) / sizeof(frontColumns[0]); i++) {
        CF(frontColumns[i], 6, 24, 1, 12, 1, BLOCK_RED_LACQUER_WOOD, 15);
    }
    const int sideZ[] = { 8, 12, 16, 20 };
    for (size_t i = 0; i < sizeof(sideZ) / sizeof(sideZ[0]); i++) {
        CF(18, 6, sideZ[i], 1, 12, 1, BLOCK_RED_LACQUER_WOOD, 15);
        CF(54, 6, sideZ[i], 1, 12, 1, BLOCK_RED_LACQUER_WOOD, 15);
    }
    const int rearColumns[] = { 25, 32, 39, 46 };
    for (size_t i = 0; i < sizeof(rearColumns) / sizeof(rearColumns[0]); i++) {
        CF(rearColumns[i], 6, 6, 1, 12, 1, BLOCK_RED_LACQUER_WOOD, 15);
    }

    /* Painted architrave, bracket arms, and gold caps. */
    CF(16, 12, 24, 41, 1, 1, BLOCK_RED_LACQUER_WOOD, 24);
    CF(15, 13, 24, 43, 1, 1, BLOCK_GREEN, 25);
    for (int bx = 18; bx <= 53; bx += 3) {
        CF(bx - 1, 14, 24, 3, 1, 1, BLOCK_RED_LACQUER_WOOD, 26);
        CF(bx, 14, 24, 1, 1, 1, BLOCK_YELLOW, 27);
        CF(bx - 1, 13, 25, 3, 1, 1, BLOCK_GREEN, 28);
        CF(bx, 14, 25, 1, 1, 1, BLOCK_YELLOW, 28);
    }
    CF(18, 12, 6, 37, 1, 1, BLOCK_RED_LACQUER_WOOD, 24);
    CF(18, 13, 6, 37, 1, 1, BLOCK_GREEN, 25);
    CF(18, 12, 6, 1, 1, 15, BLOCK_RED_LACQUER_WOOD, 24);
    CF(54, 12, 6, 1, 1, 15, BLOCK_RED_LACQUER_WOOD, 24);
    CF(16, 16, 24, 41, 1, 1, BLOCK_RED_LACQUER_WOOD, 24);
    CF(15, 17, 24, 43, 1, 1, BLOCK_GREEN, 25);
    for (int bx = 18; bx <= 53; bx += 3) {
        CF(bx - 1, 17, 24, 3, 1, 1, BLOCK_RED_LACQUER_WOOD, 26);
        CF(bx, 17, 24, 1, 1, 1, BLOCK_YELLOW, 27);
    }
    CF(18, 16, 6, 37, 1, 1, BLOCK_RED_LACQUER_WOOD, 24);
    CF(18, 17, 6, 37, 1, 1, BLOCK_GREEN, 25);
    CF(18, 16, 6, 1, 1, 15, BLOCK_RED_LACQUER_WOOD, 24);
    CF(54, 16, 6, 1, 1, 15, BLOCK_RED_LACQUER_WOOD, 24);

    /* Stone balustrade on the main terrace, open at the central stair. */
    CF(14, 6, 3, 1, 1, 28, BLOCK_DRESSED_STONE, 11);
    CF(58, 6, 3, 1, 1, 28, BLOCK_DRESSED_STONE, 11);
    CF(14, 6, 3, 45, 1, 1, BLOCK_DRESSED_STONE, 11);
    CF(14, 6, 30, 16, 1, 1, BLOCK_DRESSED_STONE, 11);
    CF(43, 6, 30, 16, 1, 1, BLOCK_DRESSED_STONE, 11);
    for (int px = 16; px <= 56; px += 8) {
        CF(px, 6, 3, 1, 2, 1, BLOCK_DRESSED_STONE, 12);
        CF(px, 6, 30, 1, 2, 1, BLOCK_DRESSED_STONE, 12);
    }

    /* Plaque above the central door, set on the porch beam, with a gold
     * frame, vertical couplets on the central columns, and a hanging frieze. */
    CF(30, 10, 23, 9, 2, 1, BLOCK_BLACK, 28);
    CP(32, 11, 23, 1, 1, 1, BLOCK_YELLOW, 2);
    CP(34, 11, 23, 1, 1, 1, BLOCK_YELLOW, 2);
    CP(36, 11, 23, 1, 1, 1, BLOCK_YELLOW, 2);
    CF(30, 9, 23, 9, 1, 1, BLOCK_YELLOW, 28);
    CF(30, 12, 23, 9, 1, 1, BLOCK_YELLOW, 28);
    CF(29, 10, 23, 1, 3, 1, BLOCK_YELLOW, 28);
    CF(39, 10, 23, 1, 3, 1, BLOCK_YELLOW, 28);
    CF(32, 6, 25, 1, 5, 1, BLOCK_BLACK, 29);
    CF(39, 6, 25, 1, 5, 1, BLOCK_BLACK, 29);
    CP(32, 7, 25, 1, 1, 1, BLOCK_YELLOW, 3);
    CP(32, 9, 25, 1, 1, 1, BLOCK_YELLOW, 3);
    CP(39, 7, 25, 1, 1, 1, BLOCK_YELLOW, 3);
    CP(39, 9, 25, 1, 1, 1, BLOCK_YELLOW, 3);
    CF(20, 11, 25, 9, 1, 1, BLOCK_WOOD_LATTICE, 29);
    CF(40, 11, 25, 12, 1, 1, BLOCK_WOOD_LATTICE, 29);
    const int bracketColumns[] = { 18, 25, 32, 39, 46, 53 };
    for (size_t i = 0; i < sizeof(bracketColumns) / sizeof(bracketColumns[0]); i++) {
        CF(bracketColumns[i] - 1, 10, 24, 3, 1, 1,
           BLOCK_RED_LACQUER_WOOD, 29);
        CF(bracketColumns[i], 10, 24, 1, 1, 1, BLOCK_YELLOW, 30);
    }
    /* Upper storey: plaque, couplets and a hanging frieze. */
    CF(30, 14, 23, 9, 2, 1, BLOCK_BLACK, 28);
    CP(32, 15, 23, 1, 1, 1, BLOCK_YELLOW, 2);
    CP(34, 15, 23, 1, 1, 1, BLOCK_YELLOW, 2);
    CP(36, 15, 23, 1, 1, 1, BLOCK_YELLOW, 2);
    CF(30, 13, 23, 9, 1, 1, BLOCK_YELLOW, 28);
    CF(30, 16, 23, 9, 1, 1, BLOCK_YELLOW, 28);
    CF(29, 14, 23, 1, 3, 1, BLOCK_YELLOW, 28);
    CF(39, 14, 23, 1, 3, 1, BLOCK_YELLOW, 28);
    CF(32, 13, 25, 1, 4, 1, BLOCK_BLACK, 29);
    CF(39, 13, 25, 1, 4, 1, BLOCK_BLACK, 29);
    CP(32, 14, 25, 1, 1, 1, BLOCK_YELLOW, 3);
    CP(32, 16, 25, 1, 1, 1, BLOCK_YELLOW, 3);
    CP(39, 14, 25, 1, 1, 1, BLOCK_YELLOW, 3);
    CP(39, 16, 25, 1, 1, 1, BLOCK_YELLOW, 3);
    CF(20, 13, 25, 9, 1, 1, BLOCK_WOOD_LATTICE, 29);
    CF(40, 13, 25, 12, 1, 1, BLOCK_WOOD_LATTICE, 29);

    if (!AppendComplexHipRoof(workspace, feature, s, 14, 3, 45, 29,
                              18, 6, 40, 2)) return false;

    /* Lower eave (yaoyan) over the front porch only.  Raising it one level
     * keeps the plaque below the eave and leaves the upper roof dominant. */
    CF(12, 13, 27, 49, 1, 5, BLOCK_BLUE_ROOF_TILE, 56);
    CF(12, 13, 25, 5, 1, 6, BLOCK_BLUE_ROOF_TILE, 56);
    CF(56, 13, 25, 5, 1, 6, BLOCK_BLUE_ROOF_TILE, 56);
    CF(14, 14, 25, 45, 1, 5, BLOCK_BLUE_ROOF_TILE, 57);
    CF(14, 14, 27, 3, 1, 4, BLOCK_BLUE_ROOF_TILE, 57);
    CF(56, 14, 27, 3, 1, 4, BLOCK_BLUE_ROOF_TILE, 57);
    CF(12, 12, 31, 49, 1, 1, BLOCK_GREEN, 58);
    CF(12, 12, 25, 1, 1, 6, BLOCK_GREEN, 58);
    CF(60, 12, 25, 1, 1, 6, BLOCK_GREEN, 58);
    CF(12, 13, 32, 49, 1, 1, BLOCK_BLUE_BRICK, 59);
    CF(11, 13, 25, 1, 1, 7, BLOCK_BLUE_BRICK, 59);
    CF(61, 13, 25, 1, 1, 7, BLOCK_BLUE_BRICK, 59);
    CF(12, 14, 31, 49, 1, 1, BLOCK_BLUE_BRICK, 60);
    for (int lx = 18; lx <= 54; lx += 12) {
        CF(lx, 6, 28, 1, 7, 1, BLOCK_RED_LACQUER_WOOD, 60);
    }
    for (int rx = 17; rx <= 55; rx += 2) {
        CF(rx, 11, 31, 1, 1, 1, BLOCK_WOOD, 62);
    }

    /* East and west side halls, both facing the courtyard.  Build the west
     * hall and mirror every x coordinate for the east hall. */
    for (int side = 0; side < 2; side++) {
#define MX(x, w) (side ? (CGA_COMPLEX_INNER_WIDTH - (x) - (w)) : (x))
        CF(MX(4, 17), 2, 32, 17, 1, 19, BLOCK_DRESSED_STONE, 10);
        CF(MX(5, 14), 3, 34, 14, 6, 16, BLOCK_WHITE_PLASTER, 12);
        CC(MX(6, 12), 4, 35, 12, 4, 14, BLOCK_AIR, 0);
        CC(MX(18, 1), 3, 40, 1, 4, 3, BLOCK_AIR, 1);
        CC(MX(18, 1), 4, 35, 1, 3, 3, BLOCK_AIR, 1);
        CC(MX(18, 1), 4, 45, 1, 3, 3, BLOCK_AIR, 1);
        CP(MX(18, 1), 3, 40, 1, 3, 2, BLOCK_RED_LACQUER_WOOD, 0);
        CP(MX(18, 1), 4, 35, 1, 3, 3, BLOCK_WOOD_LATTICE, 0);
        CP(MX(18, 1), 4, 45, 1, 3, 3, BLOCK_WOOD_LATTICE, 0);
        for (int z = 34; z <= 46; z += 4) {
            CF(MX(19, 1), 3, z, 1, 6, 1, BLOCK_RED_LACQUER_WOOD, 15);
        }
        CF(MX(19, 1), 8, 34, 1, 1, 16, BLOCK_RED_LACQUER_WOOD, 20);
        CF(MX(19, 1), 9, 34, 1, 1, 16, BLOCK_GREEN, 21);
        CF(MX(18, 1), 7, 38, 1, 2, 5, BLOCK_BLACK, 30);
        CP(MX(18, 1), 8, 39, 1, 1, 1, BLOCK_YELLOW, 5);
        CP(MX(18, 1), 8, 40, 1, 1, 1, BLOCK_YELLOW, 5);
        CP(MX(18, 1), 8, 41, 1, 1, 1, BLOCK_YELLOW, 5);
        if (!AppendComplexGableRoof(workspace, feature, s, MX(3, 19), 32,
                                    19, 20, 9, 5, 30)) return false;
#undef MX
    }

    /* Courtyard: two pines, a pair of lions at the gate, and a low incense
     * burner on the axis. */
    if (!AppendComplexPine(workspace, feature, s, 24, 39, 7) ||
        !AppendComplexPine(workspace, feature, s, 48, 39, 7) ||
        !AppendComplexLion(workspace, feature, s, 30, 59) ||
        !AppendComplexLion(workspace, feature, s, 42, 59)) return false;
    CF(35, 3, 42, 3, 1, 3, BLOCK_DRESSED_STONE, 60);
    CF(36, 4, 43, 1, 2, 1, BLOCK_BLUE_BRICK, 61);
    CF(36, 6, 43, 1, 1, 1, BLOCK_YELLOW, 62);

    /* Low screen wall (yingbi) just inside the gate.  It stops the direct
     * axis at eye level while leaving the raised main hall visible above. */
    CF(27, 2, 46, 19, 1, 2, BLOCK_DRESSED_STONE, 60);
    CF(27, 3, 46, 19, 3, 2, BLOCK_RED_LACQUER_WOOD, 61);
    CF(26, 6, 45, 21, 1, 4, BLOCK_BLUE_ROOF_TILE, 62);
    CF(26, 7, 45, 21, 1, 4, BLOCK_BLUE_BRICK, 63);
    CF(31, 3, 47, 11, 3, 1, BLOCK_DRESSED_STONE, 64);
    CF(31, 5, 47, 11, 1, 1, BLOCK_BLUE_BRICK, 65);
    CP(33, 4, 47, 1, 1, 1, BLOCK_YELLOW, 6);
    CP(36, 4, 47, 1, 1, 1, BLOCK_YELLOW, 6);
    CP(39, 4, 47, 1, 1, 1, BLOCK_YELLOW, 6);

    /* Forecourt outside the gate: clear, paved, shallow stone rail, pines. */
    CF(2, 2, CGA_COMPLEX_YARD_DEPTH - 2, 1, 1, CGA_COMPLEX_APRON - 2,
       BLOCK_DRESSED_STONE, 55);
    CF(CGA_COMPLEX_INNER_WIDTH - 3, 2, CGA_COMPLEX_YARD_DEPTH - 2, 1, 1,
       CGA_COMPLEX_APRON - 2, BLOCK_DRESSED_STONE, 55);
    CF(2, 2, CGA_COMPLEX_INNER_DEPTH - 2, 32, 1, 1, BLOCK_DRESSED_STONE, 55);
    CF(39, 2, CGA_COMPLEX_INNER_DEPTH - 2, CGA_COMPLEX_INNER_WIDTH - 41, 1, 1,
       BLOCK_DRESSED_STONE, 55);
    for (int px = 8; px <= 64; px += 8) {
        CF(px, 2, CGA_COMPLEX_YARD_DEPTH - 2, 1, 2, 1,
           BLOCK_DRESSED_STONE, 56);
    }
    CF(34, 1, CGA_COMPLEX_INNER_DEPTH - 4, 5, 1, 3, BLOCK_DRESSED_STONE, 56);
    if (!AppendComplexPine(workspace, feature, s, 10, 70, 8) ||
        !AppendComplexPine(workspace, feature, s,
                           CGA_COMPLEX_INNER_WIDTH - 11, 70, 8)) return false;

    /* Covered corridors (langwu) along the front wall, linking the gate to
     * the side halls under a low lean-to tile roof. */
    for (int side = 0; side < 2; side++) {
        int x0 = side ? 46 : 2;
        for (int cx = x0 + 2; cx <= x0 + 20; cx += 6) {
            CF(cx, 3, 55, 1, 5, 1, BLOCK_RED_LACQUER_WOOD, 60);
        }
        CF(x0 + 1, 7, 55, 23, 1, 1, BLOCK_RED_LACQUER_WOOD, 61);
        CF(x0 + 1, 8, 55, 23, 1, 1, BLOCK_GREEN, 62);
        CF(x0, 7, 53, 25, 1, 1, BLOCK_GREEN, 63);
        CF(x0, 8, 53, 25, 1, 2, BLOCK_BLUE_ROOF_TILE, 64);
        CF(x0, 9, 55, 25, 1, 2, BLOCK_BLUE_ROOF_TILE, 65);
        CF(x0, 10, 57, 25, 1, 1, BLOCK_BLUE_ROOF_TILE, 66);
    }

    /* Three-bay paifang at the forecourt entrance.  It frames the gate and
     * the main hall from the south approach without closing the axis. */
    const int paifangColumns[] = { 19, 30, 41, 52 };
    for (size_t i = 0; i < sizeof(paifangColumns) / sizeof(paifangColumns[0]); i++) {
        CF(paifangColumns[i], 3, 74, 1, 8, 1,
           BLOCK_RED_LACQUER_WOOD, 65);
        CF(paifangColumns[i] - 1, 2, 73, 3, 1, 3,
           BLOCK_DRESSED_STONE, 64);
    }
    CF(18, 9, 74, 37, 1, 1, BLOCK_RED_LACQUER_WOOD, 66);
    CF(17, 10, 74, 39, 1, 1, BLOCK_GREEN, 67);
    CF(31, 9, 73, 9, 2, 1, BLOCK_BLACK, 68);
    CP(33, 10, 73, 1, 1, 1, BLOCK_YELLOW, 4);
    CP(36, 10, 73, 1, 1, 1, BLOCK_YELLOW, 4);
    CP(39, 10, 73, 1, 1, 1, BLOCK_YELLOW, 4);
    if (!AppendComplexHipRoof(workspace, feature, s, 16, 70, 39, 9,
                              11, 3, 69, 1)) return false;

#undef CP
#undef CC
#undef CF
    return true;
}

bool CgaExpandFeature(CgaWorkspace *workspace, CgaFeature feature)
{
    switch (feature.grammarVersion) {
        case CGA_HOUSE_GRAMMAR_VERSION: return CgaExpandHouse(workspace, feature);
        case CGA_MINESHAFT_GRAMMAR_VERSION: return ExpandMineshaft(workspace, feature);
        case CGA_DUNGEON_GRAMMAR_VERSION: return ExpandDungeon(workspace, feature);
        case CGA_DESERT_TEMPLE_GRAMMAR_VERSION: return ExpandDesertTemple(workspace, feature);
        case CGA_CLASSICAL_COMPLEX_GRAMMAR_VERSION: return ExpandClassicalComplex(workspace, feature);
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

    const int complexSpacing = 320;
    const int complexHalfExtent = 54;
    int minAnchorX = FloorDivInt(startX - complexHalfExtent, complexSpacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + complexHalfExtent, complexSpacing);
    int minAnchorZ = FloorDivInt(startZ - complexHalfExtent, complexSpacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + complexHalfExtent, complexSpacing);
    for (int ax = minAnchorX; ax <= maxAnchorX; ax++) {
        for (int az = minAnchorZ; az <= maxAnchorZ; az++) {
            int x = ax * complexSpacing;
            int z = az * complexSpacing;
            Biome biome = BiomeAt(x, z);
            if ((biome != BIOME_PLAINS && biome != BIOME_FOREST) ||
                HomeSurfaceHashAt(x, z, 1543u) % 100u >= 50u ||
                TerrainHeight(x, z, mode) < 4) continue;
            /* A classical Chinese courtyard faces south.  Keep the gate
             * and the main hall on the same south-facing axis. */
            CgaOrientation orientation = CGA_ORIENTATION_0;
            int width = CGA_COMPLEX_WIDTH;
            int depth = CGA_COMPLEX_DEPTH;
            CgaScope scope = { x - width / 2, 0, z - depth / 2,
                               width, CGA_COMPLEX_HEIGHT, depth, orientation };
            if (scope.minX >= startX + CHUNK_SIZE ||
                scope.minX + scope.sizeX <= startX ||
                scope.minZ >= startZ + CHUNK_SIZE ||
                scope.minZ + scope.sizeZ <= startZ) continue;
            int base = 0;
            if (!ComplexTerrainBase(scope, mode, &base)) continue;
            scope.minY = base;
            if (!AddFeature(workspace, CGA_CLASSICAL_COMPLEX_GRAMMAR_VERSION,
                            seed, ax, az, scope)) return false;
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
            bool complexReserved = false;
            for (size_t i = 0; i < workspace->featureCount; i++) {
                if (workspace->features[i].grammarVersion ==
                        CGA_CLASSICAL_COMPLEX_GRAMMAR_VERSION &&
                    ScopeOverlapsXZ(scope, workspace->features[i].scope)) {
                    complexReserved = true;
                    break;
                }
            }
            if (complexReserved) continue;
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
