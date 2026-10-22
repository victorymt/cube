#include "world/world_location.h"

#include <limits.h>
#include <math.h>

static bool IsSurface(WorldDimension dimension)
{
    return dimension == WORLD_DIMENSION_HOME || dimension == WORLD_DIMENSION_PLANET;
}

bool WorldBlockAddressIsValid(WorldBlockAddress address)
{
    if (IsSurface(address.dimension))
        return SurfaceAddressIsValid(address.at.surface) &&
            (double)address.at.surface.radial > -(double)SURFACE_RADIUS_BLOCKS;
    return address.dimension == WORLD_DIMENSION_SPACE || address.dimension == WORLD_DIMENSION_NETHER;
}

bool WorldBlockAddressEqual(WorldBlockAddress a, WorldBlockAddress b)
{
    if (!WorldBlockAddressIsValid(a) || !WorldBlockAddressIsValid(b) || a.dimension != b.dimension)
        return false;
    if (IsSurface(a.dimension)) return SurfaceAddressEqual(a.at.surface, b.at.surface);
    return a.at.cartesian.x == b.at.cartesian.x && a.at.cartesian.y == b.at.cartesian.y &&
        a.at.cartesian.z == b.at.cartesian.z;
}

uint32_t WorldBlockAddressHash(WorldBlockAddress address)
{
    uint32_t hash = 2166136261u;
    uint32_t fields[7] = {SURFACE_CUBE_TOPOLOGY_VERSION, (uint32_t)address.dimension};
    if (IsSurface(address.dimension)) {
        fields[2] = address.at.surface.bodyId;
        fields[3] = (uint32_t)address.at.surface.face;
        fields[4] = (uint32_t)address.at.surface.u;
        fields[5] = (uint32_t)address.at.surface.v;
        fields[6] = (uint32_t)address.at.surface.radial;
    } else {
        fields[2] = (uint32_t)address.at.cartesian.x;
        fields[3] = (uint32_t)address.at.cartesian.y;
        fields[4] = (uint32_t)address.at.cartesian.z;
    }
    for (unsigned int i = 0; i < 7; i++)
        for (unsigned int shift = 0; shift < 32; shift += 8)
            hash = (hash ^ ((fields[i] >> shift) & 255u)) * 16777619u;
    return hash;
}

static int BoxFind(const WorldBlockBoxQuery *query, SurfaceAddress cell)
{
    for (unsigned i = 0; i < query->count; i++)
        if (SurfaceAddressEqual(query->cells[i].at.surface, cell))
            return (int)i;
    return -1;
}

static void BoxPush(WorldBlockBoxQuery *query, WorldDimension dimension,
                    SurfaceAddress cell)
{
    if (BoxFind(query, cell) >= 0) return;
    if (query->count >= WORLD_BLOCK_BOX_MAX_CELLS) {
        query->truncated = true;
        return;
    }
    WorldBlockAddress address;
    address.dimension = dimension;
    address.at.surface = cell;
    query->cells[query->count++] = address;
}

bool WorldBlockBoxCollect(const SurfacePose *pose, Vector3 halfSize,
                          WorldBlockBoxQuery *out)
{
    if (!out) return false;
    *out = (WorldBlockBoxQuery){0};
    if (!SurfacePoseIsValid(pose) || !isfinite(halfSize.x) ||
        !isfinite(halfSize.y) || !isfinite(halfSize.z) ||
        halfSize.x < 0.0f || halfSize.y < 0.0f || halfSize.z < 0.0f ||
        halfSize.x > 2.0f || halfSize.y > 2.0f || halfSize.z > 2.0f)
        return false;
    SurfaceBasis basis;
    if (!SurfacePoseBasis(pose, &basis)) return false;
    WorldDimension dimension = pose->bodyId == 0
        ? WORLD_DIMENSION_HOME : WORLD_DIMENSION_PLANET;

    /* Reach covers the box's physical corner distance plus one cell of slack,
     * so a cell centre just outside the box still joins the flood. */
    double reach = sqrt((double)halfSize.x * (double)halfSize.x +
                        (double)halfSize.y * (double)halfSize.y +
                        (double)halfSize.z * (double)halfSize.z) + 0.87;
    if (!isfinite(reach)) return false;

    SurfaceAddress start;
    if (!SurfaceCellFromPoint(pose->bodyId, pose->position, &start)) return false;
    BoxPush(out, dimension, start);
    /* Breadth-first over exact neighbours; the frontier stops once a cell's
     * centre is farther than the reach. A fixed visit cap keeps the cost
     * bounded even if the reach is generous. */
    unsigned visited = 0;
    for (unsigned head = 0; head < out->count && visited < 512u; head++) {
        SurfaceAddress current = out->cells[head].at.surface;
        visited++;
        SurfaceVector centre;
        if (!SurfaceCellCenter(current, &centre)) continue;
        if (SurfaceVectorLength(SurfaceVectorSubtract(centre, pose->position)) > reach)
            continue;
        for (int edge = 0; edge < 4; edge++) {
            SurfaceAddress next;
            SurfaceCubeEdge back;
            if (SurfaceCubeCellNeighbor(current, (SurfaceCubeEdge)edge, &next, &back))
                BoxPush(out, dimension, next);
        }
        SurfaceAddress up = current, down = current;
        if (up.radial < INT_MAX) up.radial++;
        if (down.radial > INT_MIN) down.radial--;
        if (SurfaceAddressIsValid(up)) BoxPush(out, dimension, up);
        if (SurfaceAddressIsValid(down)) BoxPush(out, dimension, down);
    }
    if (out->truncated) {
        /* An incomplete set must never be mistaken for a full collision answer. */
        *out = (WorldBlockBoxQuery){0};
        return false;
    }
    return true;
}

bool WorldBlockNeighbor(WorldBlockAddress address, WorldBlockSide side,
                         WorldBlockAddress *outAddress, WorldBlockSide *outReturnSide)
{
    if (!outAddress || !WorldBlockAddressIsValid(address) || (unsigned)side >= 6)
        return false;
    WorldBlockAddress next = address;
    WorldBlockSide back = (WorldBlockSide)(side ^ 1);
    if (IsSurface(address.dimension) && side < WORLD_BLOCK_DOWN) {
        SurfaceCubeEdge edge;
        if (!SurfaceCubeCellNeighbor(address.at.surface, (SurfaceCubeEdge)side,
                                      &next.at.surface, &edge)) return false;
        back = (WorldBlockSide)edge;
    } else {
        int *coordinate = IsSurface(address.dimension) ? &next.at.surface.radial
            : side < WORLD_BLOCK_V_MINUS ? &next.at.cartesian.x
            : side < WORLD_BLOCK_DOWN ? &next.at.cartesian.z : &next.at.cartesian.y;
        int delta = (side & 1) ? 1 : -1;
        if ((*coordinate == INT_MIN && delta < 0) || (*coordinate == INT_MAX && delta > 0))
            return false;
        *coordinate += delta;
    }
    if (!WorldBlockAddressIsValid(next)) return false;
    *outAddress = next;
    if (outReturnSide) *outReturnSide = back;
    return true;
}
