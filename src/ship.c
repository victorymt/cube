#include "ship.h"

#include "raymath.h"
#include "chunks.h"
#include "world.h"
#include "player.h"
#include "particles.h"
#include "space.h"
#include "space_physics.h"
#include "world_environment.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define SHIP_THRUST 30.0f
#define SHIP_CRUISE_THRUST_MULTIPLIER 4.0f
#define SHIP_ATMOSPHERE_DRAG 0.65f
#define SHIP_ASSIST_DECEL 18.0f
#define SHIP_WARP_MAX_SPEED 1200.0f
#define SHIP_WARP_ACCEL 320.0f
#define SHIP_WARP_DECEL 1200.0f
#define SHIP_WARP_STANDOFF 14.0f
#define SHIP_SYSTEM_WARP_STANDOFF 760.0f

typedef enum WarpTargetType {
    WARP_TARGET_NONE = 0,
    WARP_TARGET_PLANET,
    WARP_TARGET_SYSTEM
} WarpTargetType;

typedef struct WarpTarget {
    bool locked;
    WarpTargetType type;
    int systemAnchorX;
    int systemAnchorZ;
    int planetIndex;
    char name[48];
} WarpTarget;

static bool driving = false;
static bool cruising = false;
static bool warping = false;
static bool flightAssist = false;
static float fuel = SHIP_MAX_FUEL;
static WarpTarget warpTarget = { 0 };
static SpaceGravitySample gravityPrimary = { 0 };

static void ClearWarpTarget(void)
{
    warpTarget = (WarpTarget){ 0 };
    warping = false;
}

static bool ResolveWarpTarget(Vector3 *center, float *safeDistance)
{
    if (!warpTarget.locked) return false;

    SolarSystemDef system;
    if (!StarSystemAt(warpTarget.systemAnchorX, warpTarget.systemAnchorZ, &system)) {
        return false;
    }

    if (warpTarget.type == WARP_TARGET_SYSTEM) {
        if (center) *center = system.center;
        if (safeDistance) {
            *safeDistance = (float)system.starProxyRadius +
                            SHIP_SYSTEM_WARP_STANDOFF;
        }
        return true;
    }

    if (warpTarget.type != WARP_TARGET_PLANET || warpTarget.planetIndex < 0 ||
        warpTarget.planetIndex >= system.planetCount) return false;

    PlanetProfile profile = SolarPlanetProfile(&system, warpTarget.planetIndex);
    if (center) *center = SolarSystemPlanetCenter(&system, warpTarget.planetIndex);
    if (safeDistance) {
        *safeDistance = SolarBodyTerrainProxyRadius(profile.spaceProxyRadius) +
                        SHIP_WARP_STANDOFF;
    }
    return true;
}

static void LockWarpTarget(const Player *player, Vector3 forward)
{
    if (WorldIsSurfaceActive()) {
        SetImportMessage("Launch into space before locking a warp target.");
        return;
    }

    SpaceBodyInfo body;
    if (!SpaceBodyPick(player->position, forward, &body) || body.isStar) {
        SetImportMessage("Aim at a nearby planet to lock a warp target.");
        return;
    }

    warpTarget.locked = true;
    warpTarget.type = WARP_TARGET_PLANET;
    warpTarget.systemAnchorX = body.systemAnchorX;
    warpTarget.systemAnchorZ = body.systemAnchorZ;
    warpTarget.planetIndex = body.index - 1;
    snprintf(warpTarget.name, sizeof(warpTarget.name), "%s %c", body.name,
             'a' + (body.index > 0 ? body.index - 1 : 0));
    warping = false;
    SetImportMessage(TextFormat("Locked %s. Press G to engage warp.", warpTarget.name));
}

bool ShipBeginSystemWarp(Player *player, int systemAnchorX, int systemAnchorZ)
{
    if (!driving) {
        SetImportMessage("Board your ship before initiating a system warp.");
        return false;
    }
    if (WorldIsSurfaceActive()) {
        SetImportMessage("Launch into space before initiating a system warp.");
        return false;
    }

    SolarSystemDef system;
    if (!StarSystemAt(systemAnchorX, systemAnchorZ, &system)) {
        SetImportMessage("Selected star system is unavailable.");
        return false;
    }

    float gap = Vector3Distance(player->position, system.center) -
                ((float)system.starProxyRadius + SHIP_SYSTEM_WARP_STANDOFF);
    if (gap <= 1.0f) {
        player->velocity = Vector3Zero();
        SetImportMessage("Already within this star system's approach zone.");
        return false;
    }

    warpTarget.locked = true;
    warpTarget.type = WARP_TARGET_SYSTEM;
    warpTarget.systemAnchorX = systemAnchorX;
    warpTarget.systemAnchorZ = systemAnchorZ;
    warpTarget.planetIndex = -1;
    snprintf(warpTarget.name, sizeof(warpTarget.name), "%s", system.name);
    cruising = false;
    player->velocity = Vector3Zero();
    warping = true;
    SetImportMessage(TextFormat("System warp engaged: %s.", warpTarget.name));
    return true;
}

static void ToggleWarp(Player *player)
{
    if (warping) {
        warping = false;
        player->velocity = Vector3Zero();
        SetImportMessage("Warp cancelled.");
        return;
    }
    if (WorldIsSurfaceActive()) {
        SetImportMessage("Launch into space before engaging warp.");
        return;
    }

    Vector3 targetCenter;
    float safeDistance = 0.0f;
    if (!ResolveWarpTarget(&targetCenter, &safeDistance)) {
        ClearWarpTarget();
        SetImportMessage("Warp target is no longer available.");
        return;
    }

    float gap = Vector3Distance(player->position, targetCenter) - safeDistance;
    if (gap <= 1.0f) {
        player->velocity = Vector3Zero();
        SetImportMessage("Already at the target approach distance.");
        return;
    }

    cruising = false;
    warping = true;
    SetImportMessage(TextFormat("Warp engaged: %s.", warpTarget.name));
}

bool ShipTryEnter(int x, int y, int z, Player *player)
{
    if (driving) return false;
    if (GetBlockAt(x, y, z) != BLOCK_SPACESHIP) return false;

    SetBlock(x, y, z, BLOCK_AIR);
    player->position = (Vector3){ (float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f };
    player->velocity = Vector3Zero();
    player->floating = true;
    driving = true;
    cruising = false;
    gravityPrimary = (SpaceGravitySample){ 0 };
    ClearWarpTarget();
    SetImportMessage("Ship: inertial flight. F toggles braking assist; E exits.");
    return true;
}

bool ShipIsDriving(void)
{
    return driving;
}

bool ShipIsCruising(void)
{
    return cruising;
}

bool ShipIsWarping(void)
{
    return warping;
}

bool ShipFlightAssistEnabled(void)
{
    return flightAssist;
}

bool ShipHasGravityPrimary(void)
{
    return gravityPrimary.active;
}

const char *ShipGravityPrimaryName(void)
{
    return gravityPrimary.active ? gravityPrimary.name : "Interplanetary";
}

float ShipGravityPrimaryDistance(void)
{
    return gravityPrimary.active ? gravityPrimary.distance : 0.0f;
}

float ShipGravitySphereOfInfluence(void)
{
    return gravityPrimary.active ? gravityPrimary.encounterRadiusGame : 0.0f;
}

bool ShipHasWarpTarget(void)
{
    return warpTarget.locked;
}

bool ShipWarpTargetIsSystem(void)
{
    return warpTarget.locked && warpTarget.type == WARP_TARGET_SYSTEM;
}

const char *ShipWarpTargetName(void)
{
    return warpTarget.locked ? warpTarget.name : "---";
}

void ShipReset(void)
{
    driving = false;
    cruising = false;
    flightAssist = false;
    gravityPrimary = (SpaceGravitySample){ 0 };
    ClearWarpTarget();
    fuel = SHIP_MAX_FUEL;
}

float ShipGetFuel(void)
{
    return fuel;
}

bool ShipRefuel(void)
{
    fuel = SHIP_MAX_FUEL;
    SetImportMessage("Ship fuel restored to maximum.");
    return true;
}

bool ShipSaveState(FILE *file)
{
    return fwrite(&fuel, sizeof(fuel), 1, file) == 1;
}

bool ShipLoadState(FILE *file)
{
    float loadedFuel = 0.0f;
    if (fread(&loadedFuel, sizeof(loadedFuel), 1, file) != 1 ||
        !isfinite(loadedFuel) || loadedFuel < 0.0f || loadedFuel > SHIP_MAX_FUEL) {
        return false;
    }
    fuel = loadedFuel;
    ClearWarpTarget();
    return true;
}

void ShipToggleCruise(void)
{
    if (warping) return;
    cruising = !cruising;
    SetImportMessage(cruising ? "Cruise thrust: 4x (X to toggle)." :
                                "Cruise thrust off.");
}

static float ShipAtmosphereDensityAt(Vector3 position)
{
    if (PlanetWorldIsActive()) {
        const PlanetProfile *profile = PlanetWorldProfile();
        if (!profile || profile->atmosphereType == PLANET_ATMOSPHERE_NONE) return 0.0f;
        return Clamp(profile->atmosphereDensity *
                     (1.0f - PlanetWorldAtmosphereFade(position)), 0.0f, 1.0f);
    }
    if (HomeWorldSurfaceIsActive()) {
        return Clamp(0.78f * (1.0f - HomeWorldSpaceFade(position)), 0.0f, 1.0f);
    }
    return 0.0f;
}

static Model shipModel = { 0 };

void ShipLoadModel(void)
{
    if (shipModel.meshCount > 0) return;

    Mesh mesh = { 0 };
    mesh.vertexCount = 8 * 6 * 6;
    mesh.triangleCount = 8 * 6 * 2;
    mesh.vertices = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = malloc((size_t)mesh.vertexCount * 2 * sizeof(float));
    mesh.normals = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.colors = malloc((size_t)mesh.vertexCount * 4 * sizeof(unsigned char));
    if (!mesh.vertices || !mesh.texcoords || !mesh.normals || !mesh.colors) {
        free(mesh.vertices);
        free(mesh.texcoords);
        free(mesh.normals);
        free(mesh.colors);
        return;
    }

    int vertexIndex = 0;
    int parts[8][3] = {
        { 0, 0, -1 }, { 0, 0, 0 }, { 0, 0, 1 },
        { 0, 1, 0 },
        { -1, 0, 0 }, { 1, 0, 0 },
        { 0, 0, -2 },
        { 0, -1, -1 }
    };
    for (int i = 0; i < 8; i++) {
        for (int face = 0; face < 6; face++) {
            AddBlockFace(&mesh, &vertexIndex, parts[i][0], parts[i][1], parts[i][2],
                         face, BLOCK_SPACESHIP, WHITE, 0.0f);
        }
    }

    UploadMesh(&mesh, false);
    shipModel = LoadModelFromMesh(mesh);
    SetMaterialTexture(&shipModel.materials[0], MATERIAL_MAP_DIFFUSE, blockAtlas);
}

void ShipCleanup(void)
{
    if (shipModel.meshCount > 0) UnloadModel(shipModel);
    shipModel = (Model){ 0 };
}

void ShipDraw(const Player *player)
{
    if (shipModel.meshCount == 0) return;
    shipModel.transform = MatrixRotateXYZ((Vector3){ player->pitch, player->yaw, 0.0f });
    Vector3 pos = Vector3Add(player->position, (Vector3){ 0.0f, 0.4f, 0.0f });
    DrawModel(shipModel, pos, 1.0f, WHITE);
}

void ShipUpdate(Player *player, float dt)
{
    if (IsKeyPressed(KEY_X)) {
        if (warping) ToggleWarp(player);
        else ShipToggleCruise();
    }
    if (IsKeyPressed(KEY_R)) ShipRefuel();
    if (IsKeyPressed(KEY_F) && !warping) {
        flightAssist = !flightAssist;
        SetImportMessage(flightAssist ?
                         "Flight assist enabled: releasing thrust brakes the ship." :
                         "Flight assist disabled: inertial flight active.");
    }

    Vector2 mouseDelta = GetMouseDelta();
    if (!warping) {
        player->yaw -= mouseDelta.x * MOUSE_SENSITIVITY;
        player->pitch -= mouseDelta.y * MOUSE_SENSITIVITY;
    }
    player->pitch = Clamp(player->pitch, -1.45f, 1.45f);

    Vector3 forward = ForwardFromAngles(player->yaw, player->pitch);
    if (IsKeyPressed(KEY_Q)) LockWarpTarget(player, forward);
    if (IsKeyPressed(KEY_G)) ToggleWarp(player);

    Vector3 right = RightFromYaw(player->yaw);
    Vector3 accel = Vector3Zero();
    if (IsKeyDown(KEY_W)) accel = Vector3Add(accel, forward);
    if (IsKeyDown(KEY_S)) accel = Vector3Subtract(accel, forward);
    if (IsKeyDown(KEY_D)) accel = Vector3Add(accel, right);
    if (IsKeyDown(KEY_A)) accel = Vector3Subtract(accel, right);

    Vector3 planetDir = Vector3Zero();
    float surfaceDist = 0.0f;
    bool nearPlanet = WorldCurrentDimension() != WORLD_DIMENSION_PLANET &&
                      PlanetSurfaceAt(player->position, &planetDir, &surfaceDist,
                                      NULL);
    SpaceGravitySample gravity = { 0 };
    bool hasGravity = WorldIsSpaceActive() &&
                      SpaceGravityAt(player->position, &gravity);
    gravityPrimary = hasGravity ? gravity : (SpaceGravitySample){ 0 };
    Vector3 vertical = (Vector3){ 0.0f, 1.0f, 0.0f };
    if (nearPlanet) {
        vertical = Vector3Negate(planetDir);
    } else if (hasGravity &&
               (gravity.kind == SPACE_GRAVITY_PRIMARY_PLANET ||
                gravity.kind == SPACE_GRAVITY_PRIMARY_HOME)) {
        vertical = Vector3Normalize(Vector3Subtract(player->position,
                                                     gravity.center));
    }

    if (IsKeyDown(KEY_SPACE)) accel = Vector3Add(accel, vertical);
    if (IsKeyDown(KEY_LEFT_CONTROL)) accel = Vector3Subtract(accel, vertical);
    bool translationInput = Vector3LengthSqr(accel) > 0.0f;
    if (translationInput) accel = Vector3Normalize(accel);

    if (warping) {
        Vector3 targetCenter;
        float safeDistance = 0.0f;
        if (!ResolveWarpTarget(&targetCenter, &safeDistance)) {
            ClearWarpTarget();
            player->velocity = Vector3Zero();
            SetImportMessage("Warp target is no longer available.");
        } else {
            Vector3 toTarget = Vector3Subtract(targetCenter, player->position);
            float targetDistance = Vector3Length(toTarget);
            float gap = targetDistance - safeDistance;
            if (gap <= 1.0f || targetDistance < 0.001f) {
                warping = false;
                player->velocity = Vector3Zero();
                if (warpTarget.type == WARP_TARGET_SYSTEM) {
                    SetImportMessage(TextFormat("Arrived at %s. Lock a planet with Q.",
                                                warpTarget.name));
                } else {
                    SetImportMessage(TextFormat("Reached %s approach. Press E to land.",
                                                warpTarget.name));
                }
            } else {
                Vector3 warpDirection = Vector3Scale(toTarget, 1.0f / targetDistance);
                player->yaw = atan2f(warpDirection.x, warpDirection.z);
                player->pitch = asinf(Clamp(warpDirection.y, -1.0f, 1.0f));
                forward = warpDirection;

                float speed = Vector3Length(player->velocity);
                float maxSafeSpeed = gap / fmaxf(dt, 0.001f);
                float brakingSpeed = sqrtf(fmaxf(0.0f, 2.0f * SHIP_WARP_DECEL *
                                                        fmaxf(gap - 1.0f, 0.0f)));
                float desiredSpeed = fminf(SHIP_WARP_MAX_SPEED,
                                           fminf(brakingSpeed, maxSafeSpeed));
                float rate = desiredSpeed > speed ? SHIP_WARP_ACCEL : SHIP_WARP_DECEL;
                if (speed < desiredSpeed) speed = fminf(desiredSpeed, speed + rate * dt);
                else speed = fmaxf(desiredSpeed, speed - rate * dt);
                player->velocity = Vector3Scale(warpDirection, speed);
            }
        }
    } else {
        float thrust = SHIP_THRUST *
                       (cruising ? SHIP_CRUISE_THRUST_MULTIPLIER : 1.0f);
        player->velocity = Vector3Add(player->velocity,
                                      Vector3Scale(accel, thrust * dt));
        if (hasGravity) {
            player->velocity = Vector3Add(player->velocity,
                                          Vector3Scale(gravity.acceleration, dt));
        }

        float atmosphereDensity = ShipAtmosphereDensityAt(player->position);
        if (atmosphereDensity > 0.0f) {
            float drag = expf(-SHIP_ATMOSPHERE_DRAG * atmosphereDensity * dt);
            player->velocity = Vector3Scale(player->velocity, drag);
        }
        if (flightAssist && !translationInput) {
            Vector3 referenceVelocity = hasGravity ? gravity.primaryVelocity :
                                                     Vector3Zero();
            Vector3 relativeVelocity = Vector3Subtract(player->velocity,
                                                       referenceVelocity);
            relativeVelocity = SpacePhysicsBrakeVelocity(
                relativeVelocity, SHIP_ASSIST_DECEL, dt);
            player->velocity = Vector3Add(referenceVelocity, relativeVelocity);
        }
    }

    Vector3 delta = Vector3Scale(player->velocity, dt);
    float total = Vector3Length(delta);
    if (total > 0.001f) {
        Vector3 dir = Vector3Scale(delta, 1.0f / total);
        float remaining = total;
        while (remaining > 0.0f) {
            float stepLen = fminf(remaining, 1.0f);
            Vector3 before = player->position;
            MovePlayer(player, Vector3Scale(dir, stepLen));
            if (Vector3Distance(before, player->position) < stepLen * 0.9f) {
                player->velocity = Vector3Zero();
                if (warping) {
                    warping = false;
                    SetImportMessage("Warp halted by an obstacle.");
                }
                break;
            }
            remaining -= stepLen;
        }
    }

    if (WorldCurrentDimension() != WORLD_DIMENSION_PLANET) {
        Vector3 correctionDir = Vector3Zero();
        float correctedSurfaceDist = 0.0f;
        if (PlanetSurfaceAt(player->position, &correctionDir, &correctedSurfaceDist,
                            NULL) &&
            correctedSurfaceDist < 2.0f) {
            player->position = Vector3Add(
                player->position,
                Vector3Scale(Vector3Negate(correctionDir), 2.0f - correctedSurfaceDist));
            float inwardSpeed = Vector3DotProduct(player->velocity, correctionDir);
            if (inwardSpeed > 0.0f) {
                player->velocity = Vector3Subtract(
                    player->velocity, Vector3Scale(correctionDir, inwardSpeed));
            }
        }
    }

    int exhaustCount = (cruising || warping) ? 3 : 1;
    if (IsKeyDown(KEY_W) || warping) {
        for (int k = 0; k < exhaustCount; k++) {
            Vector3 tail = Vector3Subtract(player->position, Vector3Scale(forward, 0.9f));
            ParticlesEmitOne(tail, Vector3Negate(Vector3Scale(forward, 2.5f)),
                             (Color){ 255, 170, 60, 230 },
                             (Vector3){ 0.16f, 0.16f, 0.16f },
                             0.45f, 0.0f);
        }
        if (cruising || warping) {
            Vector3 tail = Vector3Subtract(player->position, Vector3Scale(forward, 1.4f));
            ParticlesEmitOne(tail, Vector3Negate(Vector3Scale(forward, 8.0f)),
                             warping ? (Color){ 160, 220, 255, 220 } : (Color){ 255, 220, 130, 200 },
                             warping ? (Vector3){ 0.30f, 0.30f, 0.30f } :
                                       (Vector3){ 0.22f, 0.22f, 0.22f },
                             0.6f, 0.0f);
        }
    }

    if (nearPlanet && IsKeyDown(KEY_LEFT_CONTROL)) {
        Vector3 exhaustPos = Vector3Add(player->position, Vector3Scale(vertical, 0.9f));
        ParticlesEmitOne(exhaustPos, Vector3Scale(vertical, 2.2f),
                         (Color){ 190, 220, 255, 210 },
                         (Vector3){ 0.14f, 0.14f, 0.14f },
                         0.4f, 0.0f);
    }
}

static bool FindShipPlacement(int *outX, int *outY, int *outZ, const Player *player)
{
    Vector3 forward = FlatForward(player->yaw);
    int px = (int)floorf(player->position.x + forward.x);
    int py = (int)floorf(player->position.y);
    int pz = (int)floorf(player->position.z + forward.z);
    int pAbove = py + 1;
    int pBelow = py - 1;

    int candidates[4][3] = {
        { px, py, pz },
        { px, pAbove, pz },
        { px, pBelow, pz },
        { (int)floorf(player->position.x), pAbove, (int)floorf(player->position.z) }
    };

    for (int i = 0; i < 4; i++) {
        int y = candidates[i][1];
        if (y < 0 || y >= SPACE_LAYER_TOP) continue;
        if (GetBlockAt(candidates[i][0], y, candidates[i][2]) != BLOCK_AIR) continue;
        *outX = candidates[i][0];
        *outY = y;
        *outZ = candidates[i][2];
        return true;
    }
    return false;
}

static bool FindShipLandingSpot(int *outX, int *outY, int *outZ, const Player *player)
{
    int x = (int)floorf(player->position.x);
    int z = (int)floorf(player->position.z);
    int y = (int)floorf(player->position.y);

    for (int sy = y; sy >= y - 24 && sy > 0; sy--) {
        if (sy >= SPACE_LAYER_TOP) continue;
        if (GetBlockAt(x, sy, z) != BLOCK_AIR) continue;
        if (GetBlockAt(x, sy - 1, z) == BLOCK_AIR) continue;
        *outX = x;
        *outY = sy;
        *outZ = z;
        return true;
    }
    return false;
}

static void ShipPlaceAfterExit(Player *player)
{
    int sx = 0;
    int sy = 0;
    int sz = 0;
    if (!FindShipLandingSpot(&sx, &sy, &sz, player)) {
        if (!FindShipPlacement(&sx, &sy, &sz, player)) {
            SetImportMessage("No room to place the ship - it floats away.");
            driving = false;
            cruising = false;
            gravityPrimary = (SpaceGravitySample){ 0 };
            ClearWarpTarget();
            return;
        }
    }
    SetBlock(sx, sy, sz, BLOCK_SPACESHIP);
    driving = false;
    cruising = false;
    gravityPrimary = (SpaceGravitySample){ 0 };
    ClearWarpTarget();
}

void ShipExit(Player *player)
{
    if (!driving) return;
    if (HomeWorldTryEnter(player)) {
        ShipPlaceAfterExit(player);
        return;
    }
    if (PlanetWorldTryEnter(player)) {
        driving = false;
        cruising = false;
        gravityPrimary = (SpaceGravitySample){ 0 };
        ClearWarpTarget();
        return;
    }
    if (WorldIsSpaceActive()) {
        SetImportMessage("Approach a planet before leaving the ship.");
        return;
    }
    ShipPlaceAfterExit(player);
}

void ShipForceExit(Player *player)
{
    if (!driving) return;
    ShipPlaceAfterExit(player);
}
