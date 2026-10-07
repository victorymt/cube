#include "world/surface_cube.h"

#include "raymath.h"

#include <math.h>
#include <stddef.h>

// Same face orientation as SurfaceAddressDirection. Integer axes keep edge
// transport exact; float projection cannot provide bijective block neighbors.
typedef struct CubeAxes { Vector3 normal, u, v; } CubeAxes;
static const CubeAxes axes[SURFACE_FACE_COUNT] = {
    {{ 1, 0, 0}, { 0, 0,-1}, { 0,-1, 0}},
    {{-1, 0, 0}, { 0, 0, 1}, { 0,-1, 0}},
    {{ 0, 1, 0}, { 1, 0, 0}, { 0, 0, 1}},
    {{ 0,-1, 0}, { 1, 0, 0}, { 0, 0,-1}},
    {{ 0, 0, 1}, { 1, 0, 0}, { 0,-1, 0}},
    {{ 0, 0,-1}, {-1, 0, 0}, { 0,-1, 0}}
};

bool SurfaceCubeChunkKeyIsValid(SurfaceCubeChunkKey key)
{
    return SurfaceFaceIsValid(key.face) && key.u >= 0 && key.v >= 0 &&
        key.u < SURFACE_FACE_CHUNKS && key.v < SURFACE_FACE_CHUNKS;
}

bool SurfaceCubeChunkKeyEqual(SurfaceCubeChunkKey a, SurfaceCubeChunkKey b)
{
    return a.bodyId == b.bodyId && a.face == b.face &&
        a.u == b.u && a.v == b.v;
}

uint32_t SurfaceCubeChunkKeyHash(SurfaceCubeChunkKey key)
{
    uint32_t hash = 2166136261u;
    const uint32_t fields[] = {SURFACE_CUBE_TOPOLOGY_VERSION, key.bodyId,
        (uint32_t)key.face, (uint32_t)key.u, (uint32_t)key.v};
    for (unsigned int i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        // Defined byte order, independent of struct padding and host endian.
        for (unsigned int shift = 0; shift < 32; shift += 8)
            hash = (hash ^ ((fields[i] >> shift) & 255u)) * 16777619u;
    }
    return hash;
}

bool SurfaceCubeChunkAt(SurfaceAddress cell, SurfaceCubeChunkKey *outKey)
{
    if (!outKey || !SurfaceAddressIsValid(cell)) return false;
    *outKey = (SurfaceCubeChunkKey){cell.bodyId, cell.face,
        cell.u / (SURFACE_FACE_BLOCKS / SURFACE_FACE_CHUNKS),
        cell.v / (SURFACE_FACE_BLOCKS / SURFACE_FACE_CHUNKS)};
    return true;
}

static bool Neighbor(SurfaceFace face, int u, int v, int size,
                     SurfaceCubeEdge edge, SurfaceFace *outFace,
                     int *outU, int *outV, SurfaceCubeEdge *outReturnEdge)
{
    if (!SurfaceFaceIsValid(face) || u < 0 || u >= size || v < 0 ||
        v >= size || edge < SURFACE_CUBE_U_MINUS ||
        edge > SURFACE_CUBE_V_PLUS) return false;
    int nextU = u + (edge == SURFACE_CUBE_U_PLUS) -
        (edge == SURFACE_CUBE_U_MINUS);
    int nextV = v + (edge == SURFACE_CUBE_V_PLUS) -
        (edge == SURFACE_CUBE_V_MINUS);
    SurfaceCubeEdge back = (SurfaceCubeEdge)((int)edge ^ 1);
    SurfaceFace nextFace = face;
    if (nextU < 0 || nextU >= size || nextV < 0 || nextV >= size) {
        bool crossesU = edge <= SURFACE_CUBE_U_PLUS;
        float sign = ((int)edge & 1) ? 1.0f : -1.0f;
        Vector3 normal = Vector3Scale(crossesU ? axes[face].u : axes[face].v, sign);
        for (int f = 0; f < SURFACE_FACE_COUNT; f++) {
            if (Vector3DotProduct(normal, axes[f].normal) == 1.0f) {
                nextFace = (SurfaceFace)f;
                break;
            }
        }
        Vector3 tangent = crossesU ? axes[face].v : axes[face].u;
        int along = crossesU ? v : u;
        float fixedU = Vector3DotProduct(axes[face].normal, axes[nextFace].u);
        float fixedV = Vector3DotProduct(axes[face].normal, axes[nextFace].v);
        if (fixedU != 0.0f) {
            nextU = fixedU > 0 ? size - 1 : 0;
            nextV = Vector3DotProduct(tangent, axes[nextFace].v) > 0
                ? along : size - 1 - along;
            back = fixedU > 0 ? SURFACE_CUBE_U_PLUS : SURFACE_CUBE_U_MINUS;
        } else {
            nextV = fixedV > 0 ? size - 1 : 0;
            nextU = Vector3DotProduct(tangent, axes[nextFace].u) > 0
                ? along : size - 1 - along;
            back = fixedV > 0 ? SURFACE_CUBE_V_PLUS : SURFACE_CUBE_V_MINUS;
        }
    }
    *outFace = nextFace;
    *outU = nextU;
    *outV = nextV;
    if (outReturnEdge) *outReturnEdge = back;
    return true;
}

bool SurfaceCubeCellNeighbor(SurfaceAddress cell, SurfaceCubeEdge edge,
                             SurfaceAddress *outCell,
                             SurfaceCubeEdge *outReturnEdge)
{
    if (!outCell) return false;
    SurfaceAddress next = cell;
    if (!Neighbor(cell.face, cell.u, cell.v, SURFACE_FACE_BLOCKS, edge,
                  &next.face, &next.u, &next.v, outReturnEdge)) return false;
    *outCell = next;
    return true;
}

bool SurfaceCubeChunkNeighbor(SurfaceCubeChunkKey key, SurfaceCubeEdge edge,
                              SurfaceCubeChunkKey *outKey,
                              SurfaceCubeEdge *outReturnEdge)
{
    if (!outKey) return false;
    SurfaceCubeChunkKey next = key;
    if (!Neighbor(key.face, key.u, key.v, SURFACE_FACE_CHUNKS, edge,
                  &next.face, &next.u, &next.v, outReturnEdge)) return false;
    *outKey = next;
    return true;
}

bool SurfaceCubeVertex(SurfaceFace face, float u, float v, float radial,
                       Vector3 *outPoint)
{
    if (!outPoint || !SurfaceFaceIsValid(face) || !isfinite(u) ||
        !isfinite(v) || !isfinite(radial) || u < 0 || v < 0 ||
        u > SURFACE_FACE_BLOCKS || v > SURFACE_FACE_BLOCKS ||
        radial <= -SURFACE_RADIUS_BLOCKS) return false;
    float s = u * (2.0f / SURFACE_FACE_BLOCKS) - 1.0f;
    float t = v * (2.0f / SURFACE_FACE_BLOCKS) - 1.0f;
    Vector3 point = Vector3Add(axes[face].normal,
        Vector3Add(Vector3Scale(axes[face].u, s), Vector3Scale(axes[face].v, t)));
    *outPoint = Vector3Scale(Vector3Normalize(point), SURFACE_RADIUS_BLOCKS + radial);
    return true;
}

bool SurfaceCubeNeighborhood(SurfaceCubeChunkKey center, unsigned int radius,
                             SurfaceCubeCandidate *outCandidates,
                             unsigned int capacity, unsigned int *outCount)
{
    if (outCount) *outCount = 0;
    if (!outCandidates || !outCount || !SurfaceCubeChunkKeyIsValid(center) ||
        radius > SURFACE_CUBE_NEIGHBORHOOD_RADIUS_MAX ||
        capacity < 1 + 2 * radius * (radius + 1)) return false;
    unsigned int count = 1;
    outCandidates[0] = (SurfaceCubeCandidate){center, 0};
    for (unsigned int head = 0; head < count; head++) {
        if (outCandidates[head].steps >= radius) continue;
        for (int edge = 0; edge < 4; edge++) {
            SurfaceCubeChunkKey next;
            if (!SurfaceCubeChunkNeighbor(outCandidates[head].key,
                    (SurfaceCubeEdge)edge, &next, NULL)) return false;
            bool seen = false;
            for (unsigned int i = 0; i < count; i++) {
                if (SurfaceCubeChunkKeyEqual(next, outCandidates[i].key)) {
                    seen = true;
                    break;
                }
            }
            if (seen) continue;
            if (count == capacity) return false;
            outCandidates[count++] = (SurfaceCubeCandidate){next,
                outCandidates[head].steps + 1};
        }
    }
    *outCount = count;
    return true;
}
