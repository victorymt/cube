#include "world/world_location.h"

#include <limits.h>

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
