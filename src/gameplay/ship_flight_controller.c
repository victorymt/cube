#include "gameplay/ship_flight_controller.h"

#include "raymath.h"

#include <math.h>

static bool ShipFlightVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static float ShipFlightVectorLength(Vector3 value)
{
    return sqrtf(value.x * value.x + value.y * value.y + value.z * value.z);
}

static Vector3 ShipFlightVectorCross(Vector3 left, Vector3 right)
{
    return (Vector3){
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x
    };
}

Vector3 ShipFlightApproachVelocity(Vector3 velocity, Vector3 desiredVelocity,
                                   float acceleration, float deceleration,
                                   float dt)
{
    if (!ShipFlightVectorIsFinite(velocity) ||
        !ShipFlightVectorIsFinite(desiredVelocity) ||
        !isfinite(acceleration) || !isfinite(deceleration) ||
        !isfinite(dt) || acceleration < 0.0f || deceleration < 0.0f ||
        dt <= 0.0f) {
        return velocity;
    }

    Vector3 delta = Vector3Subtract(desiredVelocity, velocity);
    float distance = Vector3Length(delta);
    if (!(distance > 0.0f) || !isfinite(distance)) return desiredVelocity;

    float currentSpeed = Vector3Length(velocity);
    float desiredSpeed = Vector3Length(desiredVelocity);
    float rate = desiredSpeed >= currentSpeed ? acceleration : deceleration;
    float step = rate * dt;
    if (step >= distance) return desiredVelocity;
    return Vector3Add(velocity, Vector3Scale(delta, step / distance));
}

Vector3 ShipFlightClampRelativeVelocity(Vector3 velocity,
                                        Vector3 referenceVelocity,
                                        float maxSpeed)
{
    if (!ShipFlightVectorIsFinite(velocity) ||
        !ShipFlightVectorIsFinite(referenceVelocity) ||
        !isfinite(maxSpeed) || maxSpeed < 0.0f) {
        return velocity;
    }
    Vector3 relative = Vector3Subtract(velocity, referenceVelocity);
    float speed = Vector3Length(relative);
    if (!(speed > maxSpeed) || !(speed > 0.0f)) return velocity;
    return Vector3Add(referenceVelocity,
                      Vector3Scale(relative, maxSpeed / speed));
}

bool ShipFlightStepCircularOrbit(const ShipCircularOrbitInput *input,
                                 ShipCircularOrbitState *out)
{
    if (!out) return false;
    *out = (ShipCircularOrbitState){ 0 };
    if (!input || !ShipFlightVectorIsFinite(input->center) ||
        !ShipFlightVectorIsFinite(input->centerVelocity) ||
        !ShipFlightVectorIsFinite(input->position) ||
        !ShipFlightVectorIsFinite(input->normal) ||
        !isfinite(input->gravitationalParameter) ||
        input->gravitationalParameter <= 0.0f ||
        !isfinite(input->radius) || input->radius <= 0.0f ||
        !isfinite(input->dt) || input->dt < 0.0f) {
        return false;
    }

    Vector3 radial = Vector3Subtract(input->position, input->center);
    float radialLength = ShipFlightVectorLength(radial);
    if (!(radialLength > 0.000001f) || !isfinite(radialLength)) return false;
    radial = Vector3Scale(radial, 1.0f / radialLength);

    Vector3 normal = Vector3Subtract(
        input->normal,
        Vector3Scale(radial, Vector3DotProduct(input->normal, radial)));
    float normalLength = ShipFlightVectorLength(normal);
    if (!(normalLength > 0.000001f) || !isfinite(normalLength)) return false;
    normal = Vector3Scale(normal, 1.0f / normalLength);

    Vector3 tangent = ShipFlightVectorCross(normal, radial);
    float tangentLength = ShipFlightVectorLength(tangent);
    if (!(tangentLength > 0.000001f) || !isfinite(tangentLength)) return false;
    tangent = Vector3Scale(tangent, 1.0f / tangentLength);
    float speed = sqrtf(input->gravitationalParameter / input->radius);
    float angle = speed * input->dt / input->radius;
    if (!isfinite(speed) || !isfinite(angle)) return false;

    float cosine = cosf(angle);
    float sine = sinf(angle);
    Vector3 nextRadial = Vector3Add(Vector3Scale(radial, cosine),
                                    Vector3Scale(tangent, sine));
    Vector3 nextTangent = Vector3Add(Vector3Scale(radial, -sine),
                                     Vector3Scale(tangent, cosine));
    out->position = Vector3Add(input->center,
                               Vector3Scale(nextRadial, input->radius));
    out->velocity = Vector3Add(input->centerVelocity,
                               Vector3Scale(nextTangent, speed));
    out->radial = nextRadial;
    out->tangent = nextTangent;
    out->speed = speed;
    return ShipFlightVectorIsFinite(out->position) &&
           ShipFlightVectorIsFinite(out->velocity);
}

bool ShipFlightGuideToTarget(const ShipFlightGuidanceInput *input,
                             ShipFlightGuidance *out)
{
    if (!out) return false;
    *out = (ShipFlightGuidance){ 0 };
    if (!input || !ShipFlightVectorIsFinite(input->position) ||
        !ShipFlightVectorIsFinite(input->velocity) ||
        !ShipFlightVectorIsFinite(input->targetPosition) ||
        !ShipFlightVectorIsFinite(input->targetVelocity) ||
        !isfinite(input->safeDistance) || input->safeDistance < 0.0f ||
        !isfinite(input->arrivalTolerance) || input->arrivalTolerance < 0.0f ||
        !isfinite(input->maxSpeed) || input->maxSpeed <= 0.0f ||
        !isfinite(input->acceleration) || input->acceleration <= 0.0f ||
        !isfinite(input->deceleration) || input->deceleration <= 0.0f ||
        !isfinite(input->dt) || input->dt <= 0.0f) {
        return false;
    }

    Vector3 offset = Vector3Subtract(input->targetPosition, input->position);
    float distance = Vector3Length(offset);
    if (!isfinite(distance)) return false;
    Vector3 relativeVelocity = Vector3Subtract(input->velocity,
                                               input->targetVelocity);
    float relativeSpeed = Vector3Length(relativeVelocity);
    float gap = fmaxf(distance - input->safeDistance, 0.0f);
    Vector3 direction = distance > 0.000001f
        ? Vector3Scale(offset, 1.0f / distance)
        : Vector3Zero();
    float closingSpeed = Vector3DotProduct(relativeVelocity, direction);
    float brakingDistance = closingSpeed > 0.0f
        ? closingSpeed * closingSpeed / (2.0f * input->deceleration)
        : 0.0f;

    out->direction = direction;
    out->distance = distance;
    out->gap = gap;
    out->relativeSpeed = relativeSpeed;
    out->closingSpeed = closingSpeed;
    out->brakingDistance = brakingDistance;
    out->etaSeconds = closingSpeed > 0.001f
        ? gap / closingSpeed
        : gap / input->maxSpeed;
    out->arrived = gap <= input->arrivalTolerance || distance <= 0.000001f;
    if (out->arrived) {
        out->velocity = input->targetVelocity;
        return true;
    }

    float brakingSpeed = sqrtf(fmaxf(
        0.0f, 2.0f * input->deceleration *
                  fmaxf(gap - input->arrivalTolerance, 0.0f)));
    float frameSafeSpeed = gap / input->dt;
    out->desiredSpeed = fminf(input->maxSpeed,
                              fminf(brakingSpeed, frameSafeSpeed));
    Vector3 desiredRelativeVelocity = Vector3Scale(direction,
                                                   out->desiredSpeed);
    Vector3 nextRelativeVelocity = ShipFlightApproachVelocity(
        relativeVelocity, desiredRelativeVelocity,
        input->acceleration, input->deceleration, input->dt);
    out->velocity = Vector3Add(input->targetVelocity, nextRelativeVelocity);
    return ShipFlightVectorIsFinite(out->velocity);
}
