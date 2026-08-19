#include "world/surface_topology.h"

#include "raymath.h"

#include <math.h>
#include <stdlib.h>

static float SurfaceClamp(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static Vector3 SurfaceCubePoint(SurfaceFace face, float s, float t)
{
    switch (face) {
        case SURFACE_FACE_POS_X: return (Vector3){ 1.0f, -t, -s };
        case SURFACE_FACE_NEG_X: return (Vector3){ -1.0f, -t, s };
        case SURFACE_FACE_POS_Y: return (Vector3){ s, 1.0f, t };
        case SURFACE_FACE_NEG_Y: return (Vector3){ s, -1.0f, -t };
        case SURFACE_FACE_POS_Z: return (Vector3){ s, -t, 1.0f };
        case SURFACE_FACE_NEG_Z: return (Vector3){ -s, -t, -1.0f };
        default: return Vector3Zero();
    }
}

static void SurfaceDirectionToFaceUv(Vector3 direction, SurfaceFace *outFace,
                                     float *outS, float *outT)
{
    float ax = fabsf(direction.x);
    float ay = fabsf(direction.y);
    float az = fabsf(direction.z);
    SurfaceFace face = SURFACE_FACE_POS_Z;
    float s = 0.0f;
    float t = 0.0f;

    if (ax >= ay && ax >= az && ax > 0.0f) {
        if (direction.x >= 0.0f) {
            face = SURFACE_FACE_POS_X;
            s = -direction.z / ax;
            t = -direction.y / ax;
        } else {
            face = SURFACE_FACE_NEG_X;
            s = direction.z / ax;
            t = -direction.y / ax;
        }
    } else if (ay >= az && ay > 0.0f) {
        if (direction.y >= 0.0f) {
            face = SURFACE_FACE_POS_Y;
            s = direction.x / ay;
            t = direction.z / ay;
        } else {
            face = SURFACE_FACE_NEG_Y;
            s = direction.x / ay;
            t = -direction.z / ay;
        }
    } else if (az > 0.0f) {
        if (direction.z >= 0.0f) {
            face = SURFACE_FACE_POS_Z;
            s = direction.x / az;
            t = -direction.y / az;
        } else {
            face = SURFACE_FACE_NEG_Z;
            s = -direction.x / az;
            t = -direction.y / az;
        }
    }

    if (outFace) *outFace = face;
    if (outS) *outS = SurfaceClamp(s, -1.0f, 1.0f);
    if (outT) *outT = SurfaceClamp(t, -1.0f, 1.0f);
}

static float SurfaceCellCoordinate(int coordinate)
{
    return ((float)coordinate + 0.5f) *
               (2.0f / (float)SURFACE_FACE_BLOCKS) -
           1.0f;
}

static int SurfaceCoordinateCell(float coordinate)
{
    float unit = (SurfaceClamp(coordinate, -1.0f, 1.0f) + 1.0f) * 0.5f;
    int cell = (int)floorf(unit * (float)SURFACE_FACE_BLOCKS);
    if (cell < 0) return 0;
    if (cell >= SURFACE_FACE_BLOCKS) return SURFACE_FACE_BLOCKS - 1;
    return cell;
}

static float SurfaceWrappedRadians(float value)
{
    value = fmodf(value + PI, 2.0f * PI);
    if (value < 0.0f) value += 2.0f * PI;
    return value - PI;
}

bool SurfaceFaceIsValid(SurfaceFace face)
{
    return face >= SURFACE_FACE_POS_X && face < SURFACE_FACE_COUNT;
}

bool SurfaceAddressIsValid(SurfaceAddress address)
{
    return SurfaceFaceIsValid(address.face) && address.u >= 0 &&
           address.u < SURFACE_FACE_BLOCKS && address.v >= 0 &&
           address.v < SURFACE_FACE_BLOCKS;
}

bool SurfaceAddressEqual(SurfaceAddress a, SurfaceAddress b)
{
    return a.bodyId == b.bodyId && a.face == b.face && a.u == b.u &&
           a.v == b.v && a.radial == b.radial;
}

Vector3 SurfaceAddressDirection(SurfaceAddress address)
{
    if (!SurfaceAddressIsValid(address)) return Vector3Zero();
    float s = SurfaceCellCoordinate(address.u);
    float t = SurfaceCellCoordinate(address.v);
    return Vector3Normalize(SurfaceCubePoint(address.face, s, t));
}

SurfaceAddress SurfaceAddressFromDirection(uint32_t bodyId, Vector3 direction,
                                           int radial)
{
    if (!isfinite(direction.x) || !isfinite(direction.y) ||
        !isfinite(direction.z) || Vector3LengthSqr(direction) <= 0.0f) {
        return (SurfaceAddress){
            .bodyId = bodyId,
            .face = SURFACE_FACE_POS_Z,
            .u = SURFACE_FACE_BLOCKS / 2,
            .v = SURFACE_FACE_BLOCKS / 2,
            .radial = radial
        };
    }
    SurfaceFace face;
    float s;
    float t;
    SurfaceDirectionToFaceUv(direction, &face, &s, &t);
    return (SurfaceAddress){
        .bodyId = bodyId,
        .face = face,
        .u = SurfaceCoordinateCell(s),
        .v = SurfaceCoordinateCell(t),
        .radial = radial
    };
}

SurfaceAddress SurfaceAddressFromLatLon(uint32_t bodyId, float longitude,
                                        float latitude, int radial)
{
    if (!isfinite(longitude)) longitude = 0.0f;
    if (!isfinite(latitude)) latitude = 0.0f;
    latitude = SurfaceClamp(latitude, -PI * 0.5f, PI * 0.5f);
    float cosLatitude = cosf(latitude);
    return SurfaceAddressFromDirection(
        bodyId,
        (Vector3){ cosLatitude * cosf(longitude), sinf(latitude),
                   cosLatitude * sinf(longitude) },
        radial);
}

SurfaceMapProjection SurfaceProjectMapCoordinates(float mapX, float mapZ)
{
    if (!isfinite(mapX)) mapX = 0.0f;
    if (!isfinite(mapZ)) mapZ = 0.0f;

    const float poleToPole = (float)SURFACE_POLE_TO_POLE_BLOCKS;
    const float latitudePeriod = poleToPole * 2.0f;
    const float pole = poleToPole * 0.5f;

    // Crossing a pole reflects latitude and rotates longitude by half a turn.
    float latitudePhase = fmodf(mapZ, latitudePeriod) + pole;
    if (latitudePhase < 0.0f) latitudePhase += latitudePeriod;
    else if (latitudePhase >= latitudePeriod) latitudePhase -= latitudePeriod;

    bool reflected = latitudePhase > poleToPole;
    float canonicalZ = reflected
        ? latitudePeriod - latitudePhase - pole
        : latitudePhase - pole;
    float longitudePhase = fmodf(
        mapX, (float)SURFACE_EQUATOR_BLOCKS);
    if (reflected) {
        longitudePhase += (float)SURFACE_EQUATOR_BLOCKS * 0.5f;
    }
    float longitude = longitudePhase *
        (2.0f * PI / (float)SURFACE_EQUATOR_BLOCKS);
    longitude = fmodf(longitude + PI, 2.0f * PI);
    if (longitude < 0.0f) longitude += 2.0f * PI;

    return (SurfaceMapProjection){
        .longitude = longitude - PI,
        .latitude = canonicalZ * (PI / poleToPole),
        // Increasing map Z runs toward canonical south in a reflected band.
        .northDirection = reflected ? -1.0f : 1.0f
    };
}

Vector2 SurfaceCanonicalMapPosition(float mapX, float mapZ,
                                    float *outNorthDirection)
{
    SurfaceMapProjection projection = SurfaceProjectMapCoordinates(
        mapX, mapZ);
    if (outNorthDirection) {
        *outNorthDirection = projection.northDirection;
    }
    return (Vector2){
        projection.longitude *
            ((float)SURFACE_EQUATOR_BLOCKS / (2.0f * PI)),
        projection.latitude *
            ((float)SURFACE_POLE_TO_POLE_BLOCKS / PI)
    };
}

SurfaceMapCell SurfaceCanonicalMapCell(float mapX, float mapZ)
{
    if (!isfinite(mapX)) mapX = 0.0f;
    if (!isfinite(mapZ)) mapZ = 0.0f;
    double x = floor((double)mapX);
    double z = floor((double)mapZ);
    const double circumference = SURFACE_EQUATOR_BLOCKS;
    const double poleToPole = SURFACE_POLE_TO_POLE_BLOCKS;
    const double pole = poleToPole * 0.5;
    const double latitudePeriod = poleToPole * 2.0;
    double phase = fmod(z + pole, latitudePeriod);
    if (phase < 0.0) phase += latitudePeriod;
    bool reflected = phase >= poleToPole;
    double canonicalZ = reflected
        ? latitudePeriod - 1.0 - phase - pole : phase - pole;
    double canonicalX = fmod(
        x + (reflected ? circumference * 0.5 : 0.0), circumference);
    if (canonicalX < 0.0) canonicalX += circumference;
    if (canonicalX >= circumference * 0.5) canonicalX -= circumference;
    return (SurfaceMapCell){ (int)canonicalX, (int)canonicalZ };
}

SurfaceSpatialKey SurfaceSpatialKeyFromMapCoordinates(
    uint32_t bodyId, float mapX, float mapZ, int radial)
{
    SurfaceMapCell cell = SurfaceCanonicalMapCell(mapX, mapZ);
    return (SurfaceSpatialKey){ bodyId, cell.x, cell.z, radial };
}

bool SurfaceSpatialKeyEqual(SurfaceSpatialKey a, SurfaceSpatialKey b)
{
    return a.bodyId == b.bodyId && a.mapX == b.mapX &&
           a.mapZ == b.mapZ && a.radial == b.radial;
}

bool SurfaceChunkKeyEqual(SurfaceChunkKey a, SurfaceChunkKey b)
{
    if (a.bodyId != b.bodyId) return false;
    for (int index = 0; index < 4; index++) {
        if (a.corners[index].x != b.corners[index].x ||
            a.corners[index].z != b.corners[index].z) {
            return false;
        }
    }
    return true;
}

SurfaceMapOffset SurfaceShortestMapOffset(float fromX, float fromZ,
                                          float toX, float toZ)
{
    SurfaceMapProjection from = SurfaceProjectMapCoordinates(fromX, fromZ);
    SurfaceMapProjection to = SurfaceProjectMapCoordinates(toX, toZ);
    float fromCosLatitude = cosf(from.latitude);
    float toCosLatitude = cosf(to.latitude);
    Vector3 fromDirection = {
        fromCosLatitude * cosf(from.longitude), sinf(from.latitude),
        fromCosLatitude * sinf(from.longitude)
    };
    Vector3 toDirection = {
        toCosLatitude * cosf(to.longitude), sinf(to.latitude),
        toCosLatitude * sinf(to.longitude)
    };
    float dot = SurfaceClamp(
        Vector3DotProduct(fromDirection, toDirection), -1.0f, 1.0f);
    Vector3 east = {
        -sinf(from.longitude), 0.0f, cosf(from.longitude)
    };
    Vector3 north = {
        -sinf(from.latitude) * cosf(from.longitude),
        cosf(from.latitude),
        -sinf(from.latitude) * sinf(from.longitude)
    };
    Vector3 tangent = Vector3Subtract(
        toDirection, Vector3Scale(fromDirection, dot));
    float tangentLength = Vector3Length(tangent);
    float angle = atan2f(tangentLength, dot);
    if (tangentLength > 0.000001f) {
        tangent = Vector3Scale(tangent, 1.0f / tangentLength);
    } else if (dot < 0.0f) {
        float longitudeDelta = SurfaceWrappedRadians(
            to.longitude - from.longitude);
        tangent = fabsf(longitudeDelta) > 0.000001f ?
            Vector3Scale(east, longitudeDelta < 0.0f ? -1.0f : 1.0f) :
            north;
    } else {
        tangent = Vector3Zero();
        angle = 0.0f;
    }
    float distance = angle * SURFACE_RADIUS_BLOCKS;
    return (SurfaceMapOffset){
        .x = Vector3DotProduct(tangent, east) * distance,
        .z = Vector3DotProduct(tangent, north) * distance *
            from.northDirection,
        .northDirection = to.northDirection
    };
}

uint32_t SurfaceCanonicalMapHash(uint32_t bodyId, float mapX, float mapZ,
                                 int radial)
{
    SurfaceSpatialKey key = SurfaceSpatialKeyFromMapCoordinates(
        bodyId, mapX, mapZ, radial);
    uint32_t hash = 2166136261u;
    hash = (hash ^ key.bodyId) * 16777619u;
    hash = (hash ^ (uint32_t)key.mapX) * 16777619u;
    hash = (hash ^ (uint32_t)key.mapZ) * 16777619u;
    hash = (hash ^ (uint32_t)key.radial) * 16777619u;
    hash ^= hash >> 15;
    return hash * 2246822519u;
}

Vector3 SurfaceDirectionFromMapCoordinates(float mapX, float mapZ)
{
    SurfaceMapProjection projection = SurfaceProjectMapCoordinates(
        mapX, mapZ);
    float cosine = cosf(projection.latitude);
    return (Vector3){
        cosine * cosf(projection.longitude),
        sinf(projection.latitude),
        cosine * sinf(projection.longitude)
    };
}

SurfaceAddress SurfaceAddressFromMapCoordinates(uint32_t bodyId, float x,
                                                float z, int radial)
{
    SurfaceMapProjection projection = SurfaceProjectMapCoordinates(x, z);
    return SurfaceAddressFromLatLon(
        bodyId, projection.longitude, projection.latitude, radial);
}

bool SurfaceAddressCanonicalMapCell(SurfaceAddress address,
                                    SurfaceMapCell *outCell)
{
    if (!outCell || !SurfaceAddressIsValid(address)) return false;
    float longitude = 0.0f;
    float latitude = 0.0f;
    SurfaceAddressLatLon(address, &longitude, &latitude);
    int estimateX = (int)floorf(longitude *
        ((float)SURFACE_EQUATOR_BLOCKS / (2.0f * PI)));
    int estimateZ = (int)floorf(latitude *
        ((float)SURFACE_POLE_TO_POLE_BLOCKS / PI));
    int bestDistance = INT32_MAX;
    SurfaceMapCell best = SurfaceCanonicalMapCell(
        (float)estimateX, (float)estimateZ);
    bool found = false;
    for (int dz = -3; dz <= 3; dz++) {
        for (int dx = -3; dx <= 3; dx++) {
            SurfaceMapCell candidate = SurfaceCanonicalMapCell(
                (float)(estimateX + dx), (float)(estimateZ + dz));
            SurfaceAddress mapped = SurfaceAddressFromMapCoordinates(
                address.bodyId, (float)candidate.x, (float)candidate.z,
                address.radial);
            if (!SurfaceAddressEqual(mapped, address)) continue;
            int distance = abs(dx) + abs(dz);
            if (!found || distance < bestDistance ||
                (distance == bestDistance && candidate.z < best.z) ||
                (distance == bestDistance && candidate.z == best.z &&
                 candidate.x < best.x)) {
                best = candidate;
                bestDistance = distance;
                found = true;
            }
        }
    }
    if (!found) return false;
    *outCell = best;
    return true;
}

void SurfaceAddressLatLon(SurfaceAddress address, float *outLongitude,
                          float *outLatitude)
{
    Vector3 direction = SurfaceAddressDirection(address);
    if (outLongitude) *outLongitude = atan2f(direction.z, direction.x);
    if (outLatitude) {
        *outLatitude = asinf(SurfaceClamp(direction.y, -1.0f, 1.0f));
    }
}

SurfaceAddress SurfaceAddressOffset(SurfaceAddress address, int deltaU,
                                    int deltaV, int deltaRadial)
{
    if (!SurfaceAddressIsValid(address)) return address;
    float step = 2.0f / (float)SURFACE_FACE_BLOCKS;
    float s = SurfaceCellCoordinate(address.u) + (float)deltaU * step;
    float t = SurfaceCellCoordinate(address.v) + (float)deltaV * step;
    SurfaceAddress moved = SurfaceAddressFromDirection(
        address.bodyId, SurfaceCubePoint(address.face, s, t),
        address.radial + deltaRadial);
    return moved;
}

SurfaceFrame SurfaceFrameAt(SurfaceAddress anchor)
{
    SurfaceFrame frame = { .anchor = anchor };
    if (!SurfaceAddressIsValid(anchor)) return frame;
    float s = SurfaceCellCoordinate(anchor.u);
    float t = SurfaceCellCoordinate(anchor.v);
    const float epsilon = 1.0f / (float)SURFACE_FACE_BLOCKS;
    frame.up = SurfaceAddressDirection(anchor);
    Vector3 west = Vector3Normalize(
        SurfaceCubePoint(anchor.face, s - epsilon, t));
    Vector3 east = Vector3Normalize(
        SurfaceCubePoint(anchor.face, s + epsilon, t));
    Vector3 south = Vector3Normalize(
        SurfaceCubePoint(anchor.face, s, t + epsilon));
    Vector3 north = Vector3Normalize(
        SurfaceCubePoint(anchor.face, s, t - epsilon));
    frame.east = Vector3Normalize(Vector3Subtract(east, west));
    frame.north = Vector3Normalize(Vector3Subtract(north, south));
    frame.north = Vector3Normalize(Vector3Subtract(
        frame.north, Vector3Scale(frame.up, Vector3DotProduct(frame.north,
                                                               frame.up))));
    frame.east = Vector3Normalize(Vector3CrossProduct(frame.north, frame.up));
    frame.origin = Vector3Scale(frame.up,
                                SURFACE_RADIUS_BLOCKS + (float)anchor.radial);
    return frame;
}

SurfaceFrame SurfaceFrameAtMapCoordinates(uint32_t bodyId, float mapX,
                                          float mapZ, int radial)
{
    SurfaceMapProjection projection = SurfaceProjectMapCoordinates(
        mapX, mapZ);
    float longitude = projection.longitude;
    float latitude = projection.latitude;
    float sinLongitude = sinf(longitude);
    float cosLongitude = cosf(longitude);
    float sinLatitude = sinf(latitude);
    float cosLatitude = cosf(latitude);
    SurfaceFrame frame = {
        .anchor = SurfaceAddressFromLatLon(bodyId, longitude, latitude,
                                            radial),
        .east = { -sinLongitude, 0.0f, cosLongitude },
        .north = {
            -sinLatitude * cosLongitude * projection.northDirection,
            cosLatitude * projection.northDirection,
            -sinLatitude * sinLongitude * projection.northDirection
        },
        .up = { cosLatitude * cosLongitude, sinLatitude,
                cosLatitude * sinLongitude }
    };
    frame.origin = Vector3Scale(
        frame.up, SURFACE_RADIUS_BLOCKS + (float)radial);
    return frame;
}

SurfaceFrame SurfaceLocalFrameAtOffset(float offsetX, float offsetZ,
                                       int radial)
{
    if (!isfinite(offsetX)) offsetX = 0.0f;
    if (!isfinite(offsetZ)) offsetZ = 0.0f;

    float distance = sqrtf(offsetX * offsetX + offsetZ * offsetZ);
    if (distance < 0.0001f) {
        return (SurfaceFrame){
            .origin = { 0.0f, (float)radial, 0.0f },
            .east = { 1.0f, 0.0f, 0.0f },
            .north = { 0.0f, 0.0f, 1.0f },
            .up = { 0.0f, 1.0f, 0.0f }
        };
    }

    float tangentX = offsetX / distance;
    float tangentZ = offsetZ / distance;
    float angle = distance / SURFACE_RADIUS_BLOCKS;
    float sinAngle = sinf(angle);
    float cosAngle = cosf(angle);
    Vector3 radialTangent = {
        tangentX * cosAngle, -sinAngle, tangentZ * cosAngle
    };
    Vector3 azimuthTangent = { -tangentZ, 0.0f, tangentX };
    Vector3 up = {
        tangentX * sinAngle, cosAngle, tangentZ * sinAngle
    };
    float radius = SURFACE_RADIUS_BLOCKS + (float)radial;
    SurfaceFrame frame = {
        .origin = {
            up.x * radius,
            up.y * radius - SURFACE_RADIUS_BLOCKS,
            up.z * radius
        },
        .east = Vector3Subtract(
            Vector3Scale(radialTangent, tangentX),
            Vector3Scale(azimuthTangent, tangentZ)),
        .north = Vector3Add(
            Vector3Scale(radialTangent, tangentZ),
            Vector3Scale(azimuthTangent, tangentX)),
        .up = up
    };
    return frame;
}

Vector3 SurfaceFrameLocalToPlanet(const SurfaceFrame *frame, Vector3 local)
{
    if (!frame) return local;
    Vector3 planet = frame->origin;
    planet = Vector3Add(planet, Vector3Scale(frame->east, local.x));
    planet = Vector3Add(planet, Vector3Scale(frame->up, local.y));
    return Vector3Add(planet, Vector3Scale(frame->north, local.z));
}

Vector3 SurfaceFramePlanetToLocal(const SurfaceFrame *frame, Vector3 planet)
{
    if (!frame) return planet;
    Vector3 offset = Vector3Subtract(planet, frame->origin);
    return (Vector3){ Vector3DotProduct(offset, frame->east),
                      Vector3DotProduct(offset, frame->up),
                      Vector3DotProduct(offset, frame->north) };
}

Vector3 SurfaceFrameTransformPoint(const SurfaceFrame *from,
                                   const SurfaceFrame *to, Vector3 point)
{
    return SurfaceFramePlanetToLocal(to,
                                     SurfaceFrameLocalToPlanet(from, point));
}

Vector3 SurfaceFrameTransformVector(const SurfaceFrame *from,
                                    const SurfaceFrame *to, Vector3 vector)
{
    if (!from || !to) return vector;
    Vector3 planet = Vector3Add(Vector3Scale(from->east, vector.x),
                                Vector3Scale(from->up, vector.y));
    planet = Vector3Add(planet, Vector3Scale(from->north, vector.z));
    return (Vector3){ Vector3DotProduct(planet, to->east),
                      Vector3DotProduct(planet, to->up),
                      Vector3DotProduct(planet, to->north) };
}

Matrix SurfacePatchTransform(SurfaceAddress reference,
                             Vector3 referenceLocalOrigin,
                             SurfaceAddress patch, int radialBase)
{
    if (!SurfaceAddressIsValid(reference) ||
        !SurfaceAddressIsValid(patch) || reference.bodyId != patch.bodyId) {
        return MatrixIdentity();
    }
    reference.radial = 0;
    patch.radial = radialBase;
    SurfaceFrame referenceFrame = SurfaceFrameAt(reference);
    SurfaceFrame patchFrame = SurfaceFrameAt(patch);
    Vector3 east = SurfaceFrameTransformVector(
        &patchFrame, &referenceFrame, (Vector3){ 1.0f, 0.0f, 0.0f });
    Vector3 up = SurfaceFrameTransformVector(
        &patchFrame, &referenceFrame, (Vector3){ 0.0f, 1.0f, 0.0f });
    Vector3 north = SurfaceFrameTransformVector(
        &patchFrame, &referenceFrame, (Vector3){ 0.0f, 0.0f, 1.0f });
    Vector3 origin = Vector3Add(
        SurfaceFramePlanetToLocal(&referenceFrame, patchFrame.origin),
        referenceLocalOrigin);
    return (Matrix){
        .m0 = east.x, .m1 = east.y, .m2 = east.z,
        .m4 = up.x, .m5 = up.y, .m6 = up.z,
        .m8 = north.x, .m9 = north.y, .m10 = north.z,
        .m12 = origin.x, .m13 = origin.y, .m14 = origin.z,
        .m15 = 1.0f
    };
}

Matrix SurfacePatchTransformAtMap(
    uint32_t bodyId, Vector2 referenceMap, Vector3 referenceLocalOrigin,
    Vector2 patchMap, int radialBase)
{
    SurfaceFrame referenceFrame = SurfaceFrameAtMapCoordinates(
        bodyId, referenceMap.x, referenceMap.y, 0);
    SurfaceFrame patchFrame = SurfaceFrameAtMapCoordinates(
        bodyId, patchMap.x, patchMap.y, radialBase);
    Vector3 east = SurfaceFrameTransformVector(
        &patchFrame, &referenceFrame, (Vector3){ 1.0f, 0.0f, 0.0f });
    Vector3 up = SurfaceFrameTransformVector(
        &patchFrame, &referenceFrame, (Vector3){ 0.0f, 1.0f, 0.0f });
    Vector3 north = SurfaceFrameTransformVector(
        &patchFrame, &referenceFrame, (Vector3){ 0.0f, 0.0f, 1.0f });
    Vector3 origin = Vector3Add(
        SurfaceFramePlanetToLocal(&referenceFrame, patchFrame.origin),
        referenceLocalOrigin);
    return (Matrix){
        .m0 = east.x, .m1 = east.y, .m2 = east.z,
        .m4 = up.x, .m5 = up.y, .m6 = up.z,
        .m8 = north.x, .m9 = north.y, .m10 = north.z,
        .m12 = origin.x, .m13 = origin.y, .m14 = origin.z,
        .m15 = 1.0f
    };
}
