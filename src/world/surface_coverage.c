#include "world/surface_coverage.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static double Angle(SurfaceVector a, SurfaceVector b)
{
    return atan2(SurfaceVectorLength(SurfaceVectorCross(a, b)), SurfaceVectorDot(a, b));
}

static bool Footprint(SurfaceCubeChunkKey key, SurfaceVector *outDirection, double *outAngle)
{
    if (!SurfaceCubeChunkKeyIsValid(key)) return false;
    const int size = SURFACE_FACE_BLOCKS / SURFACE_FACE_CHUNKS;
    double u = key.u*size, v = key.v*size;
    SurfaceVector center;
    if (!SurfacePointFromCoordinates((SurfaceCoordinates){key.face, u + size*.5, v + size*.5, 0}, &center))
        return false;
    center = SurfaceVectorNormalize(center);
    double angle = 0;
    for (int i = 0; i < 4; i++) {
        SurfaceVector corner;
        if (!SurfacePointFromCoordinates((SurfaceCoordinates){key.face,
            u + ((i & 1) ? size : 0), v + ((i & 2) ? size : 0), 0}, &corner)) return false;
        angle = fmax(angle, Angle(center, SurfaceVectorNormalize(corner)));
    }
    *outDirection = center;
    *outAngle = angle;
    return true;
}

bool SurfaceCubeChunkBounds(SurfaceCubeChunkKey key, double minRadial, double maxRadial,
                             SurfaceVector *outCenter, double *outRadius)
{
    if (!outCenter || !outRadius || !isfinite(minRadial) || !isfinite(maxRadial) ||
        minRadial > maxRadial || minRadial <= -(double)SURFACE_RADIUS_BLOCKS) return false;
    SurfaceVector direction;
    double angle;
    if (!Footprint(key, &direction, &angle)) return false;
    double low = SURFACE_RADIUS_BLOCKS + minRadial, high = SURFACE_RADIUS_BLOCKS + maxRadial;
    double middle = low*.5 + high*.5;
    if (!isfinite(high) || !isfinite(middle)) return false;
    // hypot avoids cancellation in r*r+c*c-2*r*c*cos(angle) for thin patches.
    double x = low*sin(angle), y = low*cos(angle) - middle;
    double radius = hypot(x, y);
    radius = fmax(radius, hypot(high*sin(angle), high*cos(angle) - middle));
    if (!isfinite(radius)) return false;
    *outCenter = SurfaceVectorScale(direction, middle);
    *outRadius = radius + 1e-7;
    return true;
}

static unsigned KeyIndex(SurfaceCubeChunkKey key)
{
    return ((unsigned)key.face*SURFACE_FACE_CHUNKS + (unsigned)key.v)*SURFACE_FACE_CHUNKS + (unsigned)key.u;
}

static bool MarkSeen(unsigned char *bits, SurfaceCubeChunkKey key)
{
    unsigned index = KeyIndex(key), mask = 1u << (index & 7u);
    bool seen = (bits[index >> 3u] & mask) != 0;
    bits[index >> 3u] |= (unsigned char)mask;
    return seen;
}

static SurfaceCoverageCandidate Candidate(SurfaceCubeChunkKey key, SurfaceVector focus)
{
    SurfaceVector direction;
    double patchAngle;
    // Every caller supplies a validated key from the exact neighbor primitive.
    if (!Footprint(key, &direction, &patchAngle)) return (SurfaceCoverageCandidate){.key = key, .minimumDistance = INFINITY};
    double angle = Angle(direction, focus);
    return (SurfaceCoverageCandidate){key, angle*SURFACE_RADIUS_BLOCKS,
        fmax(0, angle - patchAngle)*SURFACE_RADIUS_BLOCKS};
}

static int CompareCandidates(const void *left, const void *right)
{
    const SurfaceCoverageCandidate *a = left, *b = right;
    if (a->centerDistance != b->centerDistance) return a->centerDistance < b->centerDistance ? -1 : 1;
    if (a->key.face != b->key.face) return a->key.face < b->key.face ? -1 : 1;
    if (a->key.u != b->key.u) return a->key.u < b->key.u ? -1 : 1;
    return (a->key.v > b->key.v) - (a->key.v < b->key.v);
}

bool SurfaceCoverageCollect(uint32_t bodyId, SurfaceVector focus, double radius,
                             SurfaceCoverageCandidate *out, unsigned capacity,
                             SurfaceCoverageResult *outResult)
{
    if (!out || !outResult || !capacity || !isfinite(radius) || radius < 0 ||
        radius > SURFACE_COVERAGE_MAX_RADIUS || !SurfaceVectorIsFinite(focus) ||
        SurfaceVectorLength(focus) == 0) return false;
    focus = SurfaceVectorNormalize(focus);
    SurfaceAddress cell;
    SurfaceCubeChunkKey first;
    if (!SurfaceCellFromPoint(bodyId, SurfaceVectorScale(focus, SURFACE_RADIUS_BLOCKS), &cell) ||
        !SurfaceCubeChunkAt(cell, &first)) return false;
    SurfaceCoverageCandidate *queue = malloc(sizeof(*queue)*SURFACE_COVERAGE_MAX_CANDIDATES);
    unsigned char seen[(SURFACE_FACE_COUNT*SURFACE_FACE_CHUNKS*SURFACE_FACE_CHUNKS + 7)/8] = {0};
    if (!queue) return false;
    unsigned count = 1;
    queue[0] = Candidate(first, focus);
    MarkSeen(seen, first);
    for (unsigned head = 0; head < count; head++) for (int edge = 0; edge < 4; edge++) {
        SurfaceCubeChunkKey next;
        if (!SurfaceCubeChunkNeighbor(queue[head].key, (SurfaceCubeEdge)edge, &next, NULL)) { free(queue); return false; }
        if (MarkSeen(seen, next)) continue;
        SurfaceCoverageCandidate candidate = Candidate(next, focus);
        if (candidate.minimumDistance > radius + 1e-7) continue;
        if (count == SURFACE_COVERAGE_MAX_CANDIDATES) { free(queue); return false; }
        queue[count++] = candidate;
    }
    qsort(queue, count, sizeof(*queue), CompareCandidates);
    unsigned admitted = count < capacity ? count : capacity;
    memcpy(out, queue, admitted*sizeof(*out));
    *outResult = (SurfaceCoverageResult){count, admitted, admitted < count};
    free(queue);
    return true;
}
