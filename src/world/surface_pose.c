#include "world/surface_pose.h"

#include <limits.h>
#include <math.h>

bool SurfaceVectorIsFinite(SurfaceVector v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

double SurfaceVectorLength(SurfaceVector v) { return hypot(hypot(v.x, v.y), v.z); }
SurfaceVector SurfaceVectorAdd(SurfaceVector a, SurfaceVector b)
{ return (SurfaceVector){a.x + b.x, a.y + b.y, a.z + b.z}; }
SurfaceVector SurfaceVectorSubtract(SurfaceVector a, SurfaceVector b)
{ return (SurfaceVector){a.x - b.x, a.y - b.y, a.z - b.z}; }
SurfaceVector SurfaceVectorScale(SurfaceVector v, double s)
{ return (SurfaceVector){v.x * s, v.y * s, v.z * s}; }
double SurfaceVectorDot(SurfaceVector a, SurfaceVector b)
{ return a.x * b.x + a.y * b.y + a.z * b.z; }
SurfaceVector SurfaceVectorCross(SurfaceVector a, SurfaceVector b)
{ return (SurfaceVector){a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x}; }
SurfaceVector SurfaceVectorNormalize(SurfaceVector v)
{
    double length = SurfaceVectorLength(v);
    return length > 0 && isfinite(length) ? SurfaceVectorScale(v, 1 / length)
                                         : (SurfaceVector){0};
}

static SurfaceVector FromFloat(Vector3 v) { return (SurfaceVector){v.x, v.y, v.z}; }

bool SurfacePointFromCoordinates(SurfaceCoordinates c, SurfaceVector *outPoint)
{
    Vector3 n, u, v;
    if (!outPoint || !isfinite(c.u) || !isfinite(c.v) || !isfinite(c.radial) ||
        c.u < 0 || c.v < 0 || c.u > SURFACE_FACE_BLOCKS || c.v > SURFACE_FACE_BLOCKS ||
        c.radial <= -(double)SURFACE_RADIUS_BLOCKS ||
        !SurfaceCubeFaceAxes(c.face, &n, &u, &v)) return false;
    SurfaceVector direction = SurfaceVectorAdd(FromFloat(n), SurfaceVectorAdd(
        SurfaceVectorScale(FromFloat(u), 2*c.u / SURFACE_FACE_BLOCKS - 1),
        SurfaceVectorScale(FromFloat(v), 2*c.v / SURFACE_FACE_BLOCKS - 1)));
    SurfaceVector point = SurfaceVectorScale(SurfaceVectorNormalize(direction),
                                             SURFACE_RADIUS_BLOCKS + c.radial);
    if (!SurfaceVectorIsFinite(point)) return false;
    *outPoint = point;
    return true;
}

bool SurfaceCoordinatesFromPoint(SurfaceVector point, SurfaceCoordinates *out)
{
    double length = SurfaceVectorLength(point);
    if (!out || !SurfaceVectorIsFinite(point) || !isfinite(length) || length <= 0)
        return false;
    double x = fabs(point.x), y = fabs(point.y), z = fabs(point.z);
    SurfaceFace face = x >= y && x >= z
        ? (point.x >= 0 ? SURFACE_FACE_POS_X : SURFACE_FACE_NEG_X)
        : y >= z ? (point.y >= 0 ? SURFACE_FACE_POS_Y : SURFACE_FACE_NEG_Y)
                 : (point.z >= 0 ? SURFACE_FACE_POS_Z : SURFACE_FACE_NEG_Z);
    Vector3 n, u, v;
    if (!SurfaceCubeFaceAxes(face, &n, &u, &v)) return false;
    double major = SurfaceVectorDot(point, FromFloat(n));
    SurfaceCoordinates c = {face,
        (SurfaceVectorDot(point, FromFloat(u)) / major + 1)*.5*SURFACE_FACE_BLOCKS,
        (SurfaceVectorDot(point, FromFloat(v)) / major + 1)*.5*SURFACE_FACE_BLOCKS,
        length - SURFACE_RADIUS_BLOCKS};
    *out = c;
    return true;
}

bool SurfaceCellFromPoint(uint32_t bodyId, SurfaceVector point, SurfaceAddress *out)
{
    SurfaceCoordinates c;
    if (!out || !SurfaceCoordinatesFromPoint(point, &c) ||
        c.radial < INT_MIN || c.radial >= (double)INT_MAX + 1) return false;
    int u = (int)floor(c.u), v = (int)floor(c.v);
    // Boundary ownership is deterministic; the last vertex belongs to the
    // last cell on the selected face, never to a fictitious 4096th cell.
    if (u == SURFACE_FACE_BLOCKS) u--;
    if (v == SURFACE_FACE_BLOCKS) v--;
    SurfaceAddress cell = {bodyId, c.face, u, v, (int)floor(c.radial)};
    if (!SurfaceAddressIsValid(cell)) return false;
    *out = cell;
    return true;
}

bool SurfaceCellCenter(SurfaceAddress cell, SurfaceVector *out)
{
    return SurfaceAddressIsValid(cell) && SurfacePointFromCoordinates(
        (SurfaceCoordinates){cell.face, cell.u + .5, cell.v + .5, cell.radial + .5}, out);
}

bool SurfacePoseFromGeographic(uint32_t bodyId, double longitude,
                               double latitude, double radial, double yaw,
                               double pitch, SurfacePose *out)
{
    const double halfPi = acos(-1.0)*.5;
    if (!out || !isfinite(longitude) || !isfinite(latitude) ||
        !isfinite(radial) || !isfinite(yaw) || !isfinite(pitch) ||
        fabs(latitude) > halfPi || fabs(pitch) > halfPi ||
        radial <= -(double)SURFACE_RADIUS_BLOCKS) return false;
    double sl = sin(longitude), cl = cos(longitude);
    double st = sin(latitude), ct = cos(latitude);
    SurfaceVector up = {ct*cl, st, ct*sl};
    SurfaceVector east = {-sl, 0, cl};
    SurfaceVector north = {-st*cl, ct, -st*sl};
    SurfacePose pose = {bodyId, SurfaceVectorScale(up, SURFACE_RADIUS_BLOCKS + radial),
        {0, 0, 0}, SurfaceVectorAdd(SurfaceVectorScale(north, cos(yaw)),
                              SurfaceVectorScale(east, sin(yaw))), pitch};
    if (!SurfacePoseIsValid(&pose)) return false;
    *out = pose;
    return true;
}

bool SurfacePoseIsValid(const SurfacePose *pose)
{
    if (!pose || !SurfaceVectorIsFinite(pose->position) ||
        !SurfaceVectorIsFinite(pose->velocity) || !SurfaceVectorIsFinite(pose->heading) ||
        !isfinite(pose->pitch) || fabs(pose->pitch) > acos(-1.0)*.5) return false;
    double radius = SurfaceVectorLength(pose->position);
    return isfinite(radius) && radius > 0 &&
        fabs(SurfaceVectorLength(pose->heading) - 1) < 1e-8 &&
        fabs(SurfaceVectorDot(SurfaceVectorNormalize(pose->position), pose->heading)) < 1e-8;
}

double SurfacePoseAltitude(const SurfacePose *pose)
{ return pose ? SurfaceVectorLength(pose->position) - SURFACE_RADIUS_BLOCKS : NAN; }

bool SurfacePoseBasis(const SurfacePose *pose, SurfaceBasis *out)
{
    if (!out || !SurfacePoseIsValid(pose)) return false;
    SurfaceVector up = SurfaceVectorNormalize(pose->position);
    *out = (SurfaceBasis){pose->position,
        SurfaceVectorNormalize(SurfaceVectorCross(up, pose->heading)), up, pose->heading};
    return true;
}

Vector3 SurfacePointToLocal(const SurfaceBasis *basis, SurfaceVector point)
{
    SurfaceVector delta = SurfaceVectorSubtract(point, basis->origin);
    return (Vector3){(float)SurfaceVectorDot(delta, basis->right),
        (float)SurfaceVectorDot(delta, basis->up), (float)SurfaceVectorDot(delta, basis->forward)};
}

SurfaceVector SurfacePointFromLocal(const SurfaceBasis *basis, Vector3 point)
{
    return SurfaceVectorAdd(basis->origin, SurfaceVectorAdd(
        SurfaceVectorScale(basis->right, point.x), SurfaceVectorAdd(
        SurfaceVectorScale(basis->up, point.y), SurfaceVectorScale(basis->forward, point.z))));
}

static SurfaceVector Rotate(SurfaceVector v, SurfaceVector axis, double angle)
{
    double c = cos(angle), s = sin(angle);
    return SurfaceVectorAdd(SurfaceVectorScale(v, c), SurfaceVectorAdd(
        SurfaceVectorScale(SurfaceVectorCross(axis, v), s),
        SurfaceVectorScale(axis, SurfaceVectorDot(axis, v)*(1-c))));
}

bool SurfacePoseDisplace(const SurfacePose *pose, double right, double forward,
                          double radial, SurfacePose *out)
{
    SurfaceBasis basis;
    if (!out || !isfinite(right) || !isfinite(forward) || !isfinite(radial) ||
        !SurfacePoseBasis(pose, &basis)) return false;
    double radius = SurfaceVectorLength(pose->position);
    double distance = hypot(right, forward);
    if (!isfinite(distance) || !isfinite(radius + radial) || radius + radial <= 0)
        return false;
    SurfacePose next = *pose;
    SurfaceVector up = basis.up;
    if (distance > 0) {
        SurfaceVector tangent = SurfaceVectorAdd(SurfaceVectorScale(basis.right, right / distance),
                                                 SurfaceVectorScale(basis.forward, forward / distance));
        SurfaceVector axis = SurfaceVectorNormalize(SurfaceVectorCross(up, tangent));
        double angle = distance / radius;
        up = Rotate(up, axis, angle);
        next.heading = SurfaceVectorNormalize(Rotate(next.heading, axis, angle));
        next.velocity = Rotate(next.velocity, axis, angle);
    }
    next.position = SurfaceVectorScale(SurfaceVectorNormalize(up), radius + radial);
    if (!SurfacePoseIsValid(&next)) return false;
    *out = next;
    return true;
}
