#include "world/surface_cube.h"
#include "raymath.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static SurfaceAddress EdgeCell(int face, int edge, int along, int size)
{
    return (SurfaceAddress){37u, (SurfaceFace)face,
        edge == 0 ? 0 : edge == 1 ? size - 1 : along,
        edge == 2 ? 0 : edge == 3 ? size - 1 : along, -23};
}

static Vector3 EdgeVertex(SurfaceAddress cell, SurfaceCubeEdge edge, float along)
{
    float u = (float)cell.u;
    float v = (float)cell.v;
    if (edge <= SURFACE_CUBE_U_PLUS) {
        u += edge == SURFACE_CUBE_U_PLUS;
        v += along;
    } else {
        v += edge == SURFACE_CUBE_V_PLUS;
        u += along;
    }
    Vector3 point;
    assert(SurfaceCubeVertex(cell.face, u, v, (float)cell.radial, &point));
    return point;
}

static void TestEdges(void)
{
    for (int face = 0; face < SURFACE_FACE_COUNT; face++) {
        for (int edge = 0; edge < 4; edge++) {
            for (int along = 0; along < SURFACE_FACE_BLOCKS; along++) {
                SurfaceAddress cell = EdgeCell(face, edge, along, SURFACE_FACE_BLOCKS);
                SurfaceAddress next, back;
                SurfaceCubeEdge returnEdge;
                assert(SurfaceCubeCellNeighbor(cell, (SurfaceCubeEdge)edge, &next, &returnEdge));
                assert(next.face != cell.face);
                assert(next.bodyId == cell.bodyId && next.radial == cell.radial);
                assert(SurfaceCubeCellNeighbor(next, returnEdge, &back, NULL));
                assert(SurfaceAddressEqual(back, cell));
                Vector3 a = EdgeVertex(cell, (SurfaceCubeEdge)edge, 0);
                Vector3 b = EdgeVertex(cell, (SurfaceCubeEdge)edge, 1);
                Vector3 c = EdgeVertex(next, returnEdge, 0);
                Vector3 d = EdgeVertex(next, returnEdge, 1);
                assert((Vector3Distance(a, c) < .001f && Vector3Distance(b, d) < .001f) ||
                       (Vector3Distance(a, d) < .001f && Vector3Distance(b, c) < .001f));
                SurfaceCubeChunkKey currentChunk, nextChunk, expected;
                assert(SurfaceCubeChunkAt(cell, &currentChunk));
                assert(SurfaceCubeChunkAt(next, &nextChunk));
                assert(SurfaceCubeChunkNeighbor(currentChunk, (SurfaceCubeEdge)edge, &expected, NULL));
                assert(SurfaceCubeChunkKeyEqual(nextChunk, expected));
            }
        }
    }
}

static void TestMetric(void)
{
    // Regression: the legacy 16-map-block width near the captured pole is < .02.
    Vector3 legacyA = SurfaceFrameAtMapCoordinates(37u, 0, 4094.5f, 0).origin;
    Vector3 legacyB = SurfaceFrameAtMapCoordinates(37u, 16, 4094.5f, 0).origin;
    assert(Vector3Distance(legacyA, legacyB) < .02f);
    float minWidth = INFINITY, maxWidth = 0;
    for (int face = 0; face < SURFACE_FACE_COUNT; face++) {
        for (int v = 0; v <= SURFACE_FACE_BLOCKS; v += 16) {
            for (int u = 0; u < SURFACE_FACE_BLOCKS; u += 16) {
                Vector3 a, b;
                assert(SurfaceCubeVertex((SurfaceFace)face, (float)u, (float)v, 0, &a));
                assert(SurfaceCubeVertex((SurfaceFace)face, (float)u + 16, (float)v, 0, &b));
                float width = Vector3Distance(a, b);
                minWidth = fminf(minWidth, width);
                maxWidth = fmaxf(maxWidth, width);
                assert(width > 9.0f && width < 21.0f);
            }
        }
        SurfaceAddress cell = {37u, (SurfaceFace)face, 2048, 2048, 0};
        Vector3 center;
        assert(SurfaceCubeVertex(cell.face, 2048.5f, 2048.5f, 0, &center));
        assert(Vector3Distance(Vector3Normalize(center), SurfaceAddressDirection(cell)) < .000001f);
    }
    printf("cube 16-cell edge width: %.3f..%.3f blocks (including poles)\n", minWidth, maxWidth);
}

static void TestNeighborhoods(void)
{
    SurfaceCubeCandidate candidates[SURFACE_CUBE_NEIGHBORHOOD_CAPACITY];
    SurfaceCubeCandidate replay[SURFACE_CUBE_NEIGHBORHOOD_CAPACITY];
    const int centers[] = {0, 128, 255};
    for (int face = 0; face < SURFACE_FACE_COUNT; face++) {
        for (int u = 0; u < 3; u++) for (int v = 0; v < 3; v++) {
            SurfaceCubeChunkKey center = {37u, (SurfaceFace)face, centers[u], centers[v]};
            unsigned int count, replayCount;
            assert(SurfaceCubeNeighborhood(center, 12, candidates,
                SURFACE_CUBE_NEIGHBORHOOD_CAPACITY, &count));
            assert(SurfaceCubeNeighborhood(center, 12, replay,
                SURFACE_CUBE_NEIGHBORHOOD_CAPACITY, &replayCount));
            assert(count == replayCount && count <= 313 && count > 200);
            assert(SurfaceCubeChunkKeyEqual(candidates[0].key, center));
            for (unsigned int i = 0; i < count; i++) {
                assert(SurfaceCubeChunkKeyIsValid(candidates[i].key));
                assert(candidates[i].key.bodyId == 37u);
                assert(SurfaceCubeChunkKeyEqual(candidates[i].key, replay[i].key));
                assert(candidates[i].steps == replay[i].steps);
                if (i) assert(candidates[i].steps >= candidates[i - 1].steps);
                for (unsigned int j = 0; j < i; j++)
                    assert(!SurfaceCubeChunkKeyEqual(candidates[i].key, candidates[j].key));
                if (candidates[i].steps == 12) continue;
                for (int edge = 0; edge < 4; edge++) {
                    SurfaceCubeChunkKey next;
                    assert(SurfaceCubeChunkNeighbor(candidates[i].key, (SurfaceCubeEdge)edge, &next, NULL));
                    bool found = false;
                    for (unsigned int j = 0; j < count; j++)
                        if (SurfaceCubeChunkKeyEqual(next, candidates[j].key)) {
                            found = true;
                            assert(candidates[j].steps <= candidates[i].steps + 1);
                        }
                    assert(found);
                }
            }
        }
    }
    // At each pole, a small neighborhood must cover BOTH tangent directions.
    for (int face = SURFACE_FACE_POS_Y; face <= SURFACE_FACE_NEG_Y; face++) {
        unsigned int count;
        SurfaceCubeChunkKey center = {37u, (SurfaceFace)face, 128, 128};
        assert(SurfaceCubeNeighborhood(center, 3, candidates,
            SURFACE_CUBE_NEIGHBORHOOD_CAPACITY, &count));
        float minX = INFINITY, maxX = -INFINITY, minZ = INFINITY, maxZ = -INFINITY;
        for (unsigned int i = 0; i < count; i++) {
            SurfaceCubeChunkKey key = candidates[i].key;
            Vector3 point;
            assert(SurfaceCubeVertex(key.face, (float)(key.u * 16 + 8),
                (float)(key.v * 16 + 8), 0, &point));
            minX = fminf(minX, point.x); maxX = fmaxf(maxX, point.x);
            minZ = fminf(minZ, point.z); maxZ = fmaxf(maxZ, point.z);
        }
        assert(maxX - minX > 120 && maxZ - minZ > 120);
        printf("pole face=%d: %u chunks, span %.2f x %.2f blocks\n",
            face, count, maxX - minX, maxZ - minZ);
    }
}

static void TestInvalidAndIdentity(void)
{
    SurfaceCubeChunkKey key = {37u, SURFACE_FACE_POS_Y, 128, 128};
    SurfaceCubeChunkKey other = key;
    other.bodyId++;
    assert(!SurfaceCubeChunkKeyEqual(key, other));
    assert(SurfaceCubeChunkKeyHash(key) != SurfaceCubeChunkKeyHash(other));
    assert(SurfaceCubeChunkKeyHash(key) == 1677130179u);
    other = key; other.face = SURFACE_FACE_NEG_Y;
    assert(!SurfaceCubeChunkKeyEqual(key, other));
    unsigned int count = 999;
    SurfaceCubeCandidate output[1];
    assert(!SurfaceCubeNeighborhood(key, 3, output, 1, &count) && count == 0);
    assert(!SurfaceCubeNeighborhood(key, 999, output, 1, &count));
    assert(SurfaceCubeNeighborhood(key, 0, output, 1, &count) && count == 1);
    assert(output[0].steps == 0 && SurfaceCubeChunkKeyEqual(output[0].key, key));
    assert(!SurfaceCubeChunkNeighbor(key, (SurfaceCubeEdge)4, &other, NULL));
    key.u = -1;
    assert(!SurfaceCubeChunkNeighbor(key, SURFACE_CUBE_U_PLUS, &other, NULL));
    Vector3 point = {1, 2, 3};
    assert(!SurfaceCubeVertex(SURFACE_FACE_POS_Y, NAN, 0, 0, &point));
    assert(!SurfaceCubeVertex(SURFACE_FACE_POS_Y, 0, 0, INFINITY, &point));
    assert(!SurfaceCubeVertex(SURFACE_FACE_POS_Y, 0, 0, -SURFACE_RADIUS_BLOCKS, &point));
    assert(!SurfaceCubeVertex(SURFACE_FACE_POS_Y, -1, 0, 0, &point));
    assert(point.x == 1 && point.y == 2 && point.z == 3);
}

int main(void)
{
    TestEdges();
    TestMetric();
    TestNeighborhoods();
    TestInvalidAndIdentity();
    puts("surface cube tests passed");
    return 0;
}
