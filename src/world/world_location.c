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
    SurfaceAddress start;
    if (!SurfaceCellFromPoint(pose->bodyId, pose->position, &start)) return false;

    /* Collect candidates through the exact neighbour graph, then keep only the
     * cells whose own volume the body box truly overlaps. Graph distance alone
     * is not a volume test: it would add a ring of neighbouring cells. */
    WorldBlockBoxQuery candidates;
    candidates.count = 0;
    candidates.truncated = false;
    BoxPush(&candidates, dimension, start);
    for (unsigned head = 0; head < candidates.count && head < 256u; head++) {
        SurfaceAddress current = candidates.cells[head].at.surface;
        if (head > 512u) break;
        for (int edge = 0; edge < 4; edge++) {
            SurfaceAddress next;
            SurfaceCubeEdge back;
            if (SurfaceCubeCellNeighbor(current, (SurfaceCubeEdge)edge, &next, &back))
                BoxPush(&candidates, dimension, next);
        }
        SurfaceAddress up = current, down = current;
        if (up.radial < INT_MAX) up.radial++;
        if (down.radial > INT_MIN) down.radial--;
        if (SurfaceAddressIsValid(up)) BoxPush(&candidates, dimension, up);
        if (SurfaceAddressIsValid(down)) BoxPush(&candidates, dimension, down);
    }

    /* Exact overlap: convert the cell's eight corners into the body frame and
     * keep the cell when any corner lies inside the half-extent box, or when
     * the body centre lies inside the cell. */
    for (unsigned i = 0; i < candidates.count; i++) {
        const SurfaceAddress cell = candidates.cells[i].at.surface;
        SurfaceVector corner;
        bool overlaps = false;
        for (int du = 0; du <= 1 && !overlaps; du++)
            for (int dv = 0; dv <= 1 && !overlaps; dv++)
                for (int dr = 0; dr <= 1 && !overlaps; dr++) {
                    if (!SurfacePointFromCoordinates((SurfaceCoordinates){
                            cell.face, cell.u + du, cell.v + dv,
                            cell.radial + dr}, &corner)) continue;
                    Vector3 local = SurfacePointToLocal(&basis, corner);
                    if (fabsf(local.x) <= halfSize.x + 1e-6f &&
                        fabsf(local.y) <= halfSize.y + 1e-6f &&
                        fabsf(local.z) <= halfSize.z + 1e-6f)
                        overlaps = true;
                }
        if (!overlaps) {
            /* The body centre may sit inside the cell even when no corner is
             * inside the body (a body smaller than one cell). */
            if (cell.face == start.face && cell.u == start.u && cell.v == start.v &&
                cell.radial == start.radial) overlaps = true;
        }
        if (!overlaps) continue;
        if (out->count >= WORLD_BLOCK_BOX_MAX_CELLS) {
            *out = (WorldBlockBoxQuery){0};
            return false;
        }
        out->cells[out->count++] = candidates.cells[i];
    }
    return out->count > 0u;
}

/* A landing spot needs the cell under the feet to be solid and the cells the
 * body occupies to be clear. Unknown terrain is never treated as air: a
 * location is only clear when every occupied cell is known and not solid. */
bool WorldBlockLocationClear(const SurfacePose *pose, Vector3 halfSize,
                             WorldBlockSolidFn isSolid,
                             WorldBlockSolidFn isKnown, void *user)
{
    if (!pose || !SurfacePoseIsValid(pose) || !isKnown || !isSolid)
        return false;
    /* Body cells: `pose->position` is the body centre, so the box is exactly
     * the physical volume the body occupies. A standing body's box must not
     * reach into the ground cell: the caller places the centre at
     * ground top + halfSize.y. */
    WorldBlockBoxQuery body;
    if (!WorldBlockBoxCollect(pose, halfSize, &body)) return false;
    if (!isKnown(&body, user)) return false;
    if (isSolid(&body, user)) return false;

    /* Ground cell: the cell immediately below the body's lowest occupied cell,
     * not merely one below the centre. Using the lowest enumerated cell keeps
     * this correct for tall and short bodies alike. */
    SurfaceAddress feet;
    if (!SurfaceCellFromPoint(pose->bodyId, pose->position, &feet)) return false;
    for (unsigned i = 0; i < body.count; i++) {
        const SurfaceAddress cell = body.cells[i].at.surface;
        if (cell.face == feet.face && cell.u == feet.u && cell.v == feet.v &&
            cell.radial < feet.radial) {
            feet.radial = cell.radial;
        }
    }
    if (feet.radial <= INT_MIN) return false;
    SurfaceAddress below = feet;
    below.radial--;
    if (!SurfaceAddressIsValid(below)) return false;
    WorldBlockAddress ground;
    ground.dimension = feet.bodyId == 0 ? WORLD_DIMENSION_HOME
                                        : WORLD_DIMENSION_PLANET;
    ground.at.surface = below;
    WorldBlockBoxQuery groundBox;
    groundBox.count = 1;
    groundBox.truncated = false;
    groundBox.cells[0] = ground;
    return isKnown(&groundBox, user) && isSolid(&groundBox, user);
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
