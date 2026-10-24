#include "world/world_surface_snapshot.h"
#include "world/chunks_internal.h"
#include "world/surface_pose.h"
#include "world/surface_topology.h"

// The halo is sampled once on the main thread. Workers never follow pointers
// into resident columns, so eviction cannot invalidate a mesh job's input.
void CaptureCubeBoundary(SurfaceBoundarySnapshot *snapshot,
                          SurfaceCubeChunkKey key, int sectionY)
{
    memset(snapshot, 0, sizeof(*snapshot));
    if (!SurfaceCubeChunkKeyIsValid(key)) return;
    for (int x = -1; x <= CHUNK_SIZE; x++) for (int z = -1; z <= CHUNK_SIZE; z++) {
        SurfaceAddress cell = {key.bodyId, key.face,
            key.u * CHUNK_SIZE + (x < 0 ? 0 : x >= CHUNK_SIZE ? CHUNK_SIZE - 1 : x),
            key.v * CHUNK_SIZE + (z < 0 ? 0 : z >= CHUNK_SIZE ? CHUNK_SIZE - 1 : z), 0};
        SurfaceCubeEdge vertical = z < 0 ? SURFACE_CUBE_V_MINUS : SURFACE_CUBE_V_PLUS;
        if (x < 0 || x >= CHUNK_SIZE) {
            SurfaceCubeEdge edge = x < 0 ? SURFACE_CUBE_U_MINUS : SURFACE_CUBE_U_PLUS;
            // Corner halo follows U then transported V; cube corner paths
            // do not commute. Face-neighbor samples remain exact inverses.
            if (!SurfaceCubeTransportEdge(cell, edge, vertical, &vertical) ||
                !SurfaceCubeCellNeighbor(cell, edge, &cell, NULL)) continue;
        }
        if ((z < 0 || z >= CHUNK_SIZE) &&
            !SurfaceCubeCellNeighbor(cell, vertical, &cell, NULL)) continue;
        SurfaceCubeChunkKey neighborKey;
        if (!SurfaceCubeChunkAt(cell, &neighborKey)) continue;
        const Chunk *neighbor = FindCubeChunk(neighborKey);
        if (!neighbor || neighbor->cubeWorldGeneration != WorldCurrentGeneration()) continue;
        int lx = cell.u % CHUNK_SIZE, lz = cell.v % CHUNK_SIZE;
        for (int y = -1; y <= SURFACE_SECTION_HEIGHT; y++) {
            if (x >= 0 && x < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE &&
                y >= 0 && y < SURFACE_SECTION_HEIGHT) continue;
            int radial = sectionY * SURFACE_SECTION_HEIGHT + y;
            int sy = SurfaceSectionYFromBlockY(radial);
            BlockType block = BLOCK_AIR;
            if (!ChunkTryGetLocalBlock(neighbor, lx, radial, lz, &block) &&
                !ChunkTerrainSectionIsResolved(neighbor, sy)) continue;
            snapshot->blocks[x+1][y+1][z+1] = (unsigned short)block;
            snapshot->known[x+1][y+1][z+1] = 1;
            if (block == BLOCK_WATER) {
                const ChunkSection *section = ChunkGetSectionConst(neighbor, sy);
                int index = (lx * SURFACE_SECTION_HEIGHT + SurfaceSectionLocalYFromBlockY(radial)) * CHUNK_SIZE + lz;
                snapshot->volumes[x+1][y+1][z+1] = section && section->waterVolumes
                    ? section->waterVolumes[index] : WATER_VOLUME_CAPACITY;
            }
        }
    }
}

// Body-axis vectors relative to a double-precision section anchor. This keeps
// mesh coordinates small without introducing a singular polar tangent frame.
void CurveCubeMeshData(Mesh *mesh, SurfaceCubeChunkKey key, int sectionY)
{
    if (!mesh || !mesh->vertices || mesh->vertexCount <= 0) return;
    SurfaceVector anchor;
    double radial = sectionY * SURFACE_SECTION_HEIGHT;
    if (!SurfacePointFromCoordinates((SurfaceCoordinates){key.face,
        key.u * CHUNK_SIZE, key.v * CHUNK_SIZE, radial}, &anchor)) return;
    /* Frame at the anchor. Its axes are the ones the render transform uses. */
    SurfaceFrame base = SurfaceFrameAt((SurfaceAddress){key.bodyId, key.face,
        key.u * CHUNK_SIZE, key.v * CHUNK_SIZE, (int)radial});
    SurfaceVector baseEast = {(double)base.east.x, (double)base.east.y, (double)base.east.z};
    SurfaceVector baseUp = {(double)base.up.x, (double)base.up.y, (double)base.up.z};
    SurfaceVector baseNorth = {(double)base.north.x, (double)base.north.y, (double)base.north.z};
    for (int i = 0; i < mesh->vertexCount; i++) {
        float *v = mesh->vertices + i * 3;
        SurfaceVector point;
        if (!SurfacePointFromCoordinates((SurfaceCoordinates){key.face, v[0], v[2], radial + v[1]}, &point)) continue;
        /* The renderer treats a vertex as a vector in the anchor's tangent
         * frame (x=east, y=up, z=north), so the Cartesian offset must be
         * projected onto that frame. Storing the raw offset instead squashed a
         * purely radial 10-block step to y=1.16, which stretched every vertical
         * face into a streak. */
        SurfaceVector offset = SurfaceVectorSubtract(point, anchor);
        v[0] = (float)SurfaceVectorDot(offset, baseEast);
        v[1] = (float)SurfaceVectorDot(offset, baseUp);
        v[2] = (float)SurfaceVectorDot(offset, baseNorth);
    }
    // Projection shears side faces; transforming the old Cartesian normal
    // as a direction would give incorrect lighting. Use each actual triangle.
    if (mesh->normals) for (int i = 0; i + 2 < mesh->vertexCount; i += 3) {
        const float *v = mesh->vertices + i * 3;
        Vector3 a = {v[0], v[1], v[2]}, b = {v[3], v[4], v[5]}, c = {v[6], v[7], v[8]};
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        for (int j = 0; j < 3; j++) {
            float *out = mesh->normals + (i + j) * 3;
            out[0] = n.x; out[1] = n.y; out[2] = n.z;
        }
    }
}
