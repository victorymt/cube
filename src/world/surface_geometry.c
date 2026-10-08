#include "world/surface_geometry.h"

#include <math.h>

static SurfaceVector TriangleNormal(SurfaceTriangle t)
{
    return SurfaceVectorNormalize(SurfaceVectorCross(
        SurfaceVectorSubtract(t.b, t.a), SurfaceVectorSubtract(t.c, t.a)));
}

bool SurfaceCellTriangles(SurfaceAddress cell, SurfaceCellBox box,
                           SurfaceTriangle out[12])
{
    if (!out || !SurfaceAddressIsValid(cell) ||
        !isfinite(box.minU) || !isfinite(box.maxU) ||
        !isfinite(box.minV) || !isfinite(box.maxV) ||
        !isfinite(box.minRadial) || !isfinite(box.maxRadial) ||
        box.minU < 0 || box.minV < 0 || box.minRadial < 0 ||
        box.maxU > 1 || box.maxV > 1 || box.maxRadial > 1 ||
        box.minU >= box.maxU || box.minV >= box.maxV || box.minRadial >= box.maxRadial)
        return false;
    SurfaceVector vertices[8], center = {0};
    for (unsigned i = 0; i < 8; i++) {
        SurfaceCoordinates c = {cell.face,
            cell.u + ((i & 1) ? box.maxU : box.minU),
            cell.v + ((i & 2) ? box.maxV : box.minV),
            cell.radial + ((i & 4) ? box.maxRadial : box.minRadial)};
        if (!SurfacePointFromCoordinates(c, &vertices[i])) return false;
        center = SurfaceVectorAdd(center, SurfaceVectorScale(vertices[i], .125));
    }
    static const unsigned char faces[6][4] = {
        {0, 2, 6, 4}, {1, 5, 7, 3}, {0, 4, 5, 1},
        {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 6, 7, 5}
    };
    for (unsigned side = 0; side < 6; side++) for (unsigned half = 0; half < 2; half++) {
        SurfaceTriangle t = {vertices[faces[side][0]], vertices[faces[side][half + 1]],
            vertices[faces[side][half + 2]], (WorldBlockSide)side};
        if (SurfaceVectorDot(TriangleNormal(t), SurfaceVectorSubtract(t.a, center)) < 0) {
            SurfaceVector swap = t.b; t.b = t.c; t.c = swap;
        }
        out[side*2 + half] = t;
    }
    return true;
}

static bool RayTriangle(SurfaceVector origin, SurfaceVector direction,
                         SurfaceTriangle triangle, double *outDistance)
{
    SurfaceVector e1 = SurfaceVectorSubtract(triangle.b, triangle.a);
    SurfaceVector e2 = SurfaceVectorSubtract(triangle.c, triangle.a);
    SurfaceVector p = SurfaceVectorCross(direction, e2);
    double det = SurfaceVectorDot(e1, p);
    if (fabs(det) < 1e-12) return false;
    SurfaceVector from = SurfaceVectorSubtract(origin, triangle.a);
    double u = SurfaceVectorDot(from, p) / det;
    if (u < -1e-9 || u > 1 + 1e-9) return false;
    SurfaceVector q = SurfaceVectorCross(from, e1);
    double v = SurfaceVectorDot(direction, q) / det;
    if (v < -1e-9 || u + v > 1 + 1e-9) return false;
    double t = SurfaceVectorDot(e2, q) / det;
    if (!isfinite(t) || t < -1e-9) return false;
    *outDistance = fmax(0, t);
    return true;
}

bool SurfaceRayCellBox(SurfaceAddress cell, SurfaceCellBox box,
                       SurfaceVector origin, SurfaceVector direction,
                       double maxDistance, SurfaceGeometryHit *out)
{
    if (!out || !SurfaceVectorIsFinite(origin) || !SurfaceVectorIsFinite(direction) ||
        SurfaceVectorLength(direction) == 0 || !isfinite(maxDistance) || maxDistance < 0) return false;
    SurfaceTriangle triangles[12];
    if (!SurfaceCellTriangles(cell, box, triangles)) return false;
    direction = SurfaceVectorNormalize(direction);
    bool hit = false;
    SurfaceGeometryHit result = {.distance = maxDistance};
    for (unsigned i = 0; i < 12; i++) {
        double distance;
        if (RayTriangle(origin, direction, triangles[i], &distance) &&
            distance <= result.distance && (!hit || distance < result.distance - 1e-10)) {
            hit = true;
            result = (SurfaceGeometryHit){distance,
                SurfaceVectorAdd(origin, SurfaceVectorScale(direction, distance)),
                TriangleNormal(triangles[i]), triangles[i].side};
        }
    }
    if (hit) *out = result;
    return hit;
}

static bool BasisValid(const SurfaceBasis *basis)
{
    if (!basis || !SurfaceVectorIsFinite(basis->origin) || !SurfaceVectorIsFinite(basis->up) ||
        !SurfaceVectorIsFinite(basis->right) || !SurfaceVectorIsFinite(basis->forward)) return false;
    return fabs(SurfaceVectorLength(basis->right) - 1) < 1e-8 &&
        fabs(SurfaceVectorLength(basis->up) - 1) < 1e-8 &&
        fabs(SurfaceVectorLength(basis->forward) - 1) < 1e-8 &&
        fabs(SurfaceVectorDot(basis->right, basis->up)) < 1e-8 &&
        fabs(SurfaceVectorDot(basis->right, basis->forward)) < 1e-8 &&
        fabs(SurfaceVectorDot(basis->up, basis->forward)) < 1e-8;
}

static SurfaceVector LocalPoint(const SurfaceBasis *b, SurfaceVector point)
{
    SurfaceVector v = SurfaceVectorSubtract(point, b->origin);
    return (SurfaceVector){SurfaceVectorDot(v, b->right), SurfaceVectorDot(v, b->up),
        SurfaceVectorDot(v, b->forward)};
}

static SurfaceVector PlanetVector(const SurfaceBasis *b, SurfaceVector v)
{
    return SurfaceVectorAdd(SurfaceVectorScale(b->right, v.x), SurfaceVectorAdd(
        SurfaceVectorScale(b->up, v.y), SurfaceVectorScale(b->forward, v.z)));
}

static bool SweepAxis(SurfaceVector axis, SurfaceTriangle t, SurfaceVector half,
                       SurfaceVector delta, double *enter, double *leave,
                       SurfaceVector *normal)
{
    double length = SurfaceVectorLength(axis);
    if (length < 1e-12) return true;
    axis = SurfaceVectorScale(axis, 1 / length);
    double a = SurfaceVectorDot(t.a, axis), b = SurfaceVectorDot(t.b, axis), c = SurfaceVectorDot(t.c, axis);
    double extent = fabs(axis.x)*half.x + fabs(axis.y)*half.y + fabs(axis.z)*half.z;
    double low = fmin(a, fmin(b, c)) - extent, high = fmax(a, fmax(b, c)) + extent;
    double speed = SurfaceVectorDot(delta, axis);
    if (fabs(speed) < 1e-12) return low <= 1e-9 && high >= -1e-9;
    double first = low/speed, last = high/speed;
    if (first > last) { double swap = first; first = last; last = swap; }
    if (first > *enter) {
        *enter = first;
        *normal = SurfaceVectorScale(axis, speed > 0 ? -1 : 1);
    }
    *leave = fmin(*leave, last);
    return *enter <= *leave + 1e-10;
}

bool SurfaceSweepCellBox(SurfaceAddress cell, SurfaceCellBox shape,
                         const SurfaceBasis *basis, Vector3 halfSize,
                         Vector3 displacement, SurfaceGeometryHit *out)
{
    SurfaceVector half = {halfSize.x, halfSize.y, halfSize.z};
    SurfaceVector delta = {displacement.x, displacement.y, displacement.z};
    if (!out || !BasisValid(basis) || !SurfaceVectorIsFinite(half) ||
        !SurfaceVectorIsFinite(delta) || half.x <= 0 || half.y <= 0 || half.z <= 0) return false;
    SurfaceTriangle triangles[12];
    if (!SurfaceCellTriangles(cell, shape, triangles)) return false;
    const SurfaceVector axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    bool hit = false;
    SurfaceGeometryHit best = {.distance = 1};
    for (int i = 0; i < 12; i++) {
        SurfaceTriangle t = {LocalPoint(basis, triangles[i].a), LocalPoint(basis, triangles[i].b),
            LocalPoint(basis, triangles[i].c), triangles[i].side};
        SurfaceVector edges[3] = {SurfaceVectorSubtract(t.b, t.a),
            SurfaceVectorSubtract(t.c, t.b), SurfaceVectorSubtract(t.a, t.c)};
        double enter = 0, leave = best.distance;
        SurfaceVector normal = TriangleNormal(t);
        bool intersects = SweepAxis(normal, t, half, delta, &enter, &leave, &normal);
        for (int a = 0; a < 3 && intersects; a++) {
            intersects = SweepAxis(axes[a], t, half, delta, &enter, &leave, &normal);
            for (int e = 0; e < 3 && intersects; e++)
                intersects = SweepAxis(SurfaceVectorCross(edges[e], axes[a]), t, half, delta,
                    &enter, &leave, &normal);
        }
        if (intersects && enter <= leave && enter <= best.distance && (!hit || enter < best.distance - 1e-10)) {
            hit = true;
            best = (SurfaceGeometryHit){enter,
                SurfaceVectorAdd(basis->origin, PlanetVector(basis, SurfaceVectorScale(delta, enter))),
                PlanetVector(basis, normal), t.side};
        }
    }
    if (hit) *out = best;
    return hit;
}
