#include "gameplay/ship.h"

#include "gameplay/ship_ground_effects.h"
#include "gameplay/ship_flight_controller.h"
#include "gameplay/ship_navigation.h"
#include "gameplay/ship_visual_internal.h"

#include "raymath.h"
#include "world/world.h"
#include "gameplay/player.h"
#include "space/space.h"
#include "space/space_units.h"
#include "space/space_physics.h"
#include "world/world_environment.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define SHIP_SURFACE_THRUST 30.0f
#define SHIP_SPACE_MANEUVER_THRUST 3.0f
#define SHIP_SPACE_MANEUVER_MAX_SPEED 8.0f
#define SHIP_MANUAL_CRUISE_INITIAL_SPEED 12.0f
#define SHIP_MANUAL_CRUISE_REVERSE_SPEED 10.0f
#define SHIP_CRUISE_MAX_SPEED 40.0f
#define SHIP_CRUISE_SPEED_CHANGE 12.0f
#define SHIP_CRUISE_ACCEL 6.0f
#define SHIP_CRUISE_DECEL 10.0f
#define SHIP_SUPERCRUISE_MAX_SPEED 1200.0f
#define SHIP_SUPERCRUISE_ACCEL 180.0f
#define SHIP_SUPERCRUISE_DECEL 360.0f
#define SHIP_ATMOSPHERE_DRAG 0.65f
#define SHIP_SURFACE_ASSIST_DECEL 18.0f
#define SHIP_SPACE_ASSIST_DECEL 5.0f
#define SHIP_INTERSTELLAR_MAX_SPEED 6000.0f
#define SHIP_INTERSTELLAR_ACCEL 900.0f
#define SHIP_INTERSTELLAR_DECEL 1800.0f
#define SHIP_PLANET_CRUISE_MIN_TOLERANCE 0.05f
#define SHIP_SPACE_COLLISION_CLEARANCE 0.00025f
#define SHIP_THRUST_FUEL_RATE 0.08f
#define SHIP_CRUISE_FUEL_RATE 0.16f
#define SHIP_SUPERCRUISE_FUEL_RATE 0.24f
#define SHIP_INTERSTELLAR_FUEL_RATE 0.45f

typedef enum NavigationTargetType {
    NAVIGATION_TARGET_NONE = 0,
    NAVIGATION_TARGET_PLANET,
    NAVIGATION_TARGET_SYSTEM
} NavigationTargetType;

typedef enum NavigationIntent {
    NAVIGATION_INTENT_CONTEXTUAL = 0,
    NAVIGATION_INTENT_INTERSTELLAR
} NavigationIntent;

typedef struct NavigationTarget {
    bool locked;
    NavigationTargetType type;
    int systemAnchorX;
    int systemAnchorZ;
    uint32_t bodyId;
    int planetIndex;
    char name[48];
} NavigationTarget;

typedef struct ShipOrbitState {
    bool active;
    Vector3 normal;
    Vector3 radial;
    float radius;
    float gravitationalParameter;
} ShipOrbitState;

static bool driving = false;
static ShipDriveMode driveMode = SHIP_DRIVE_MANEUVER;
static bool flightAssist = false;
static float fuel = SHIP_MAX_FUEL;
static NavigationTarget navigationTarget = { 0 };
static ShipOrbitState orbitState = { 0 };
static NavigationIntent navigationIntent = NAVIGATION_INTENT_CONTEXTUAL;
static SpaceGravitySample gravityPrimary = { 0 };
static float cruiseSetSpeed = 0.0f;
static float relativeSpeed = 0.0f;
static float targetSpeed = 0.0f;
static float targetClosingSpeed = 0.0f;
static float targetBrakingDistance = 0.0f;
static float targetEtaSeconds = 0.0f;

static float NavigationArrivalTolerance(float safeDistance)
{
    if (navigationTarget.type == NAVIGATION_TARGET_SYSTEM) return 1.0f;
    return fmaxf(safeDistance * 0.05f,
                 SHIP_PLANET_CRUISE_MIN_TOLERANCE);
}

static bool ShipDriveIsGuided(void)
{
    return driveMode == SHIP_DRIVE_APPROACH ||
           driveMode == SHIP_DRIVE_SUPERCRUISE ||
           driveMode == SHIP_DRIVE_INTERSTELLAR_WARP;
}

static bool ShipDriveIsWarpTransit(void)
{
    return driveMode == SHIP_DRIVE_SUPERCRUISE ||
           driveMode == SHIP_DRIVE_INTERSTELLAR_WARP;
}

static void ClearOrbitState(void)
{
    orbitState = (ShipOrbitState){ 0 };
}

static bool ResolveNavigationTarget(ShipDriveMode mode, Vector3 *center,
                                    Vector3 *velocity, float *safeDistance);

static bool ShipDirectionForCoreBlock(BlockType type, ShipDirection *out)
{
    if (type < BLOCK_SPACESHIP_CORE_NORTH ||
        type > BLOCK_SPACESHIP_CORE_WEST) return false;
    if (out) {
        *out = (ShipDirection)((int)type - (int)BLOCK_SPACESHIP_CORE_NORTH);
    }
    return true;
}

ShipDirection ShipDirectionFromYaw(float yaw)
{
    if (!isfinite(yaw)) return SHIP_DIRECTION_NORTH;
    yaw = fmodf(yaw, 2.0f * PI);
    int quarterTurns = (int)lroundf(yaw / (PI * 0.5f));
    quarterTurns %= 4;
    if (quarterTurns < 0) quarterTurns += 4;
    return (ShipDirection)quarterTurns;
}

BlockType ShipCoreBlockForDirection(ShipDirection direction)
{
    int value = (int)direction;
    if (value < (int)SHIP_DIRECTION_NORTH ||
        value > (int)SHIP_DIRECTION_WEST) value = (int)SHIP_DIRECTION_NORTH;
    return (BlockType)((int)BLOCK_SPACESHIP_CORE_NORTH + value);
}

bool ShipBlockIsParkedCore(BlockType type)
{
    return ShipDirectionForCoreBlock(type, NULL);
}

bool ShipBlockIsOccupied(BlockType type)
{
    return type == BLOCK_SPACESHIP_OCCUPIED;
}

static bool ShipFootprintContains(const ParkedShip *ship, int x, int y, int z)
{
    return ship && x >= ship->coreX - 1 && x <= ship->coreX + 2 &&
           y >= ship->coreY && y < ship->coreY + SHIP_FOOTPRINT_HEIGHT &&
           z >= ship->coreZ - 1 && z <= ship->coreZ + 2;
}

static bool ShipCoreCoordinatesValid(int x, int y, int z)
{
    return x > INT_MIN && x <= INT_MAX - 2 &&
           y <= INT_MAX - SHIP_FOOTPRINT_HEIGHT &&
           z > INT_MIN && z <= INT_MAX - 2;
}

bool ShipResolveParkedAt(int x, int y, int z, ParkedShip *out)
{
    if (out) *out = (ParkedShip){ 0 };
    BlockType hit = GetBlockAt(x, y, z);
    if (hit == BLOCK_SPACESHIP) {
        if (out) {
            *out = (ParkedShip){
                .coreX = x, .coreY = y, .coreZ = z,
                .direction = SHIP_DIRECTION_NORTH, .legacy = true
            };
        }
        return true;
    }

    ShipDirection direction;
    if (ShipDirectionForCoreBlock(hit, &direction)) {
        if (out) {
            *out = (ParkedShip){
                .coreX = x, .coreY = y, .coreZ = z,
                .direction = direction, .legacy = false
            };
        }
        return true;
    }
    if (hit != BLOCK_SPACESHIP_OCCUPIED) return false;

    if (x <= INT_MIN + 2 || x >= INT_MAX - 1 ||
        y <= INT_MIN + SHIP_FOOTPRINT_HEIGHT ||
        z <= INT_MIN + 2 || z >= INT_MAX - 1) return false;
    for (int coreY = y - (SHIP_FOOTPRINT_HEIGHT - 1); coreY <= y; coreY++) {
        for (int coreX = x - 2; coreX <= x + 1; coreX++) {
            for (int coreZ = z - 2; coreZ <= z + 1; coreZ++) {
                BlockType core = GetBlockAt(coreX, coreY, coreZ);
                if (!ShipDirectionForCoreBlock(core, &direction)) continue;
                ParkedShip candidate = {
                    .coreX = coreX, .coreY = coreY, .coreZ = coreZ,
                    .direction = direction, .legacy = false
                };
                if (!ShipFootprintContains(&candidate, x, y, z)) continue;
                if (out) *out = candidate;
                return true;
            }
        }
    }
    return false;
}

static bool ShipCellOverlapsPlayer(int x, int y, int z, const Player *player)
{
    if (!player) return false;
    float playerMinX = player->position.x - PLAYER_RADIUS;
    float playerMaxX = player->position.x + PLAYER_RADIUS;
    float playerMinY = player->position.y;
    float playerMaxY = player->position.y + PLAYER_HEIGHT;
    float playerMinZ = player->position.z - PLAYER_RADIUS;
    float playerMaxZ = player->position.z + PLAYER_RADIUS;
    return playerMaxX > (float)x && playerMinX < (float)x + 1.0f &&
           playerMaxY > (float)y && playerMinY < (float)y + 1.0f &&
           playerMaxZ > (float)z && playerMinZ < (float)z + 1.0f;
}

bool ShipCanPlaceParked(int coreX, int coreY, int coreZ,
                        ShipDirection direction, const Player *player)
{
    if ((int)direction < (int)SHIP_DIRECTION_NORTH ||
        (int)direction > (int)SHIP_DIRECTION_WEST) return false;
    if (!ShipCoreCoordinatesValid(coreX, coreY, coreZ)) return false;
    WorldBlockRegion coreRegion = WorldBlockRegionAt(coreY);
    if (coreRegion == WORLD_BLOCK_REGION_NONE) return false;

    for (int y = coreY; y < coreY + SHIP_FOOTPRINT_HEIGHT; y++) {
        if (WorldBlockRegionAt(y) != coreRegion) return false;
        for (int x = coreX - 1; x <= coreX + 2; x++) {
            for (int z = coreZ - 1; z <= coreZ + 2; z++) {
                if (coreRegion == WORLD_BLOCK_REGION_SPACE &&
                    !SpaceBlockReadyAt(x, y, z)) return false;
                if (GetBlockAt(x, y, z) != BLOCK_AIR ||
                    ShipCellOverlapsPlayer(x, y, z, player)) return false;
            }
        }
    }
    return true;
}

static void ShipSetCell(int x, int y, int z, BlockType type, bool recordUndo)
{
    if (recordUndo) SetBlock(x, y, z, type);
    else SetBlockNoUndo(x, y, z, type);
}

bool ShipPlaceParked(int coreX, int coreY, int coreZ,
                     ShipDirection direction, bool recordUndo)
{
    if (!ShipCanPlaceParked(coreX, coreY, coreZ, direction, NULL)) return false;
    if (recordUndo) WorldBeginUndoGroup();
    ShipSetCell(coreX, coreY, coreZ,
                ShipCoreBlockForDirection(direction), recordUndo);
    for (int y = coreY; y < coreY + SHIP_FOOTPRINT_HEIGHT; y++) {
        for (int x = coreX - 1; x <= coreX + 2; x++) {
            for (int z = coreZ - 1; z <= coreZ + 2; z++) {
                if (x == coreX && y == coreY && z == coreZ) continue;
                ShipSetCell(x, y, z, BLOCK_SPACESHIP_OCCUPIED, recordUndo);
            }
        }
    }
    if (recordUndo) WorldEndUndoGroup();
    ShipTrackParkedAt(coreX, coreY, coreZ);
    return true;
}

bool ShipRemoveParkedAt(int x, int y, int z, bool recordUndo)
{
    ParkedShip ship;
    if (!ShipResolveParkedAt(x, y, z, &ship)) return false;
    if (recordUndo) WorldBeginUndoGroup();
    if (ship.legacy) {
        ShipSetCell(ship.coreX, ship.coreY, ship.coreZ, BLOCK_AIR, recordUndo);
    } else {
        ShipSetCell(ship.coreX, ship.coreY, ship.coreZ, BLOCK_AIR, recordUndo);
        for (int cellY = ship.coreY;
             cellY < ship.coreY + SHIP_FOOTPRINT_HEIGHT; cellY++) {
            for (int cellX = ship.coreX - 1; cellX <= ship.coreX + 2; cellX++) {
                for (int cellZ = ship.coreZ - 1; cellZ <= ship.coreZ + 2; cellZ++) {
                    if (cellX == ship.coreX && cellY == ship.coreY &&
                        cellZ == ship.coreZ) continue;
                    if (GetBlockAt(cellX, cellY, cellZ) == BLOCK_SPACESHIP_OCCUPIED) {
                        ShipSetCell(cellX, cellY, cellZ, BLOCK_AIR, recordUndo);
                    }
                }
            }
        }
    }
    if (recordUndo) WorldEndUndoGroup();
    ShipForgetParkedAt(ship.coreX, ship.coreY, ship.coreZ);
    return true;
}

static ShipLocatorContext ShipLocatorContextAt(float y)
{
    WorldBlockRegion region = WorldBlockRegionAt((int)floorf(y));
    WorldDimension dimension = WORLD_DIMENSION_HOME;
    if (region == WORLD_BLOCK_REGION_SPACE) dimension = WORLD_DIMENSION_SPACE;
    else if (region == WORLD_BLOCK_REGION_NETHER) dimension = WORLD_DIMENSION_NETHER;
    else if (PlanetWorldIsActive()) dimension = WORLD_DIMENSION_PLANET;
    else if (!HomeWorldSurfaceIsActive()) dimension = WORLD_DIMENSION_SPACE;

    return (ShipLocatorContext){
        .dimension = dimension,
        .surfaceId = dimension == WORLD_DIMENSION_PLANET ? WorldCurrentSurfaceId() : 0u,
        .spaceOriginX = SpaceOriginX(),
        .spaceOriginZ = SpaceOriginZ()
    };
}

static const char *ShipLocatorLocationName(WorldDimension dimension)
{
    switch (dimension) {
    case WORLD_DIMENSION_HOME: return "Homeworld";
    case WORLD_DIMENSION_PLANET: return PlanetWorldName();
    case WORLD_DIMENSION_SPACE: return "Deep space";
    case WORLD_DIMENSION_NETHER: return "Nether";
    default: return "Unknown";
    }
}

void ShipTrackParkedAt(int x, int y, int z)
{
    ShipLocatorContext context = ShipLocatorContextAt((float)y + 0.5f);
    ShipLocatorRecordParked(context, x, y, z,
                            ShipLocatorLocationName(context.dimension));
}

void ShipForgetParkedAt(int x, int y, int z)
{
    ShipLocatorRemoveIfMatches(ShipLocatorContextAt((float)y + 0.5f), x, y, z);
}

bool ShipLocatorTargetAt(Vector3 observer, ShipLocatorTarget *out)
{
    if (!ShipLocatorResolve(ShipLocatorContextAt(observer.y), observer, out)) {
        return false;
    }
    if (out && out->status == SHIP_LOCATOR_TARGET_LOCAL &&
        ShipBlockIsParkedCore(GetBlockAt(out->blockX, out->blockY, out->blockZ))) {
        out->position.x += 0.5f;
        out->position.y += 0.25f;
        out->position.z += 0.5f;
        out->distance = Vector3Distance(observer, out->position);
    }
    return true;
}

static void ClearNavigationTarget(void)
{
    navigationTarget = (NavigationTarget){ 0 };
    navigationIntent = NAVIGATION_INTENT_CONTEXTUAL;
    if (ShipDriveIsGuided() || driveMode == SHIP_DRIVE_ORBIT) {
        driveMode = SHIP_DRIVE_MANEUVER;
    }
    ClearOrbitState();
    targetSpeed = 0.0f;
    targetClosingSpeed = 0.0f;
    targetBrakingDistance = 0.0f;
    targetEtaSeconds = 0.0f;
}

static float NavigationPlanetDistance(const SolarSystemDef *system,
                                      int planetIndex, ShipDriveMode mode)
{
    if (!system || planetIndex < 0 || planetIndex >= system->planetCount) {
        return 0.0f;
    }
    switch (mode) {
    case SHIP_DRIVE_INTERSTELLAR_WARP:
        return SolarSystemPlanetEncounterRadiusGame(system, planetIndex);
    case SHIP_DRIVE_SUPERCRUISE:
        return SolarSystemPlanetSupercruiseExitRadiusGame(system, planetIndex);
    default:
        return SolarSystemPlanetParkingRadiusGame(system, planetIndex);
    }
}

static bool NavigationPositionClearOfSatellites(
    Vector3 position, const SpaceSatelliteInfo *satellites, int count,
    int systemAnchorX, int systemAnchorZ, int planetIndex)
{
    for (int i = 0; i < count; i++) {
        const SpaceSatelliteInfo *satellite = &satellites[i];
        if (satellite->systemAnchorX != systemAnchorX ||
            satellite->systemAnchorZ != systemAnchorZ ||
            satellite->parentPlanetIndex != planetIndex) continue;
        float physicalRadius = (float)SpaceUnitsKilometersToGameDistance(
            satellite->physicalRadiusKm);
        float clearance = fmaxf(satellite->encounterRadiusGame,
                                physicalRadius * 2.20f) + 0.5f;
        if (Vector3Distance(position, satellite->center) < clearance) {
            return false;
        }
    }
    return true;
}

static Vector3 NavigationArrivalPosition(const SolarSystemDef *system,
                                         int planetIndex, Vector3 center,
                                         Vector3 currentPosition,
                                         float parkingRadius)
{
    if (!system || planetIndex < 0 || !(parkingRadius > 0.0f) ||
        !isfinite(parkingRadius)) return currentPosition;

    Vector3 outward = Vector3Subtract(currentPosition, center);
    if (Vector3LengthSqr(outward) < 0.000001f) {
        outward = (Vector3){ 0.0f, 1.0f, 0.0f };
    } else {
        outward = Vector3Normalize(outward);
    }

    float queryRadius = fminf(SOLAR_SYSTEM_QUERY_RADIUS,
                              fmaxf(parkingRadius + 64.0f, 128.0f));
    SpaceSatelliteInfo satellites[48];
    int satelliteCount = SpaceSatellitesNear(center, queryRadius,
                                             satellites, 48);
    Vector3 directions[7] = {
        outward,
        { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }
    };
    for (int i = 0; i < 7; i++) {
        Vector3 direction = Vector3Normalize(directions[i]);
        Vector3 candidate = Vector3Add(
            center, Vector3Scale(direction, parkingRadius));
        if (NavigationPositionClearOfSatellites(
                candidate, satellites, satelliteCount,
                system->anchorX, system->anchorZ, planetIndex)) {
            return candidate;
        }
    }
    return Vector3Add(center, Vector3Scale(outward, parkingRadius));
}

static int NavigationPlanetIndexForSystem(const SolarSystemDef *system)
{
    if (!system || navigationTarget.type != NAVIGATION_TARGET_PLANET) return -1;
    if (navigationTarget.bodyId != 0u) {
        for (int i = 0; i < system->planetCount; i++) {
            if (system->planets[i].bodyId == navigationTarget.bodyId) return i;
        }
        return -1;
    }
    return navigationTarget.planetIndex >= 0 &&
           navigationTarget.planetIndex < system->planetCount
        ? navigationTarget.planetIndex : -1;
}

static bool EstablishNavigationOrbit(Player *player,
                                     const SolarSystemDef *system,
                                     int planetIndex, Vector3 center,
                                     Vector3 centerVelocity,
                                     float parkingRadius,
                                     bool *hasSolidSurface)
{
    if (!player || !system || planetIndex < 0 ||
        planetIndex >= system->planetCount) return false;
    PlanetProfile profile = SolarPlanetProfile(system, planetIndex);
    float mu = (float)SpaceUnitsGravitationalParameterGame(profile.massKg);
    if (!(mu > 0.0f) || !isfinite(mu) || !(parkingRadius > 0.0f)) return false;

    Vector3 arrival = NavigationArrivalPosition(
        system, planetIndex, center, player->position, parkingRadius);
    Vector3 radial = Vector3Subtract(arrival, center);
    float radialLength = Vector3Length(radial);
    if (!(radialLength > 0.000001f) || !isfinite(radialLength)) return false;
    radial = Vector3Scale(radial, 1.0f / radialLength);

    Vector3 tangent = Vector3Subtract(
        centerVelocity,
        Vector3Scale(radial, Vector3DotProduct(centerVelocity, radial)));
    if (Vector3LengthSqr(tangent) < 0.000001f) {
        Vector3 axis = fabsf(radial.y) < 0.90f
            ? (Vector3){ 0.0f, 1.0f, 0.0f }
            : (Vector3){ 1.0f, 0.0f, 0.0f };
        tangent = Vector3CrossProduct(axis, radial);
    }
    tangent = Vector3Normalize(tangent);
    Vector3 normal = Vector3Normalize(Vector3CrossProduct(radial, tangent));

    ShipCircularOrbitState orbit;
    if (!ShipFlightStepCircularOrbit(&(ShipCircularOrbitInput){
            .center = center,
            .centerVelocity = centerVelocity,
            .position = arrival,
            .normal = normal,
            .gravitationalParameter = mu,
            .radius = parkingRadius,
            .dt = 0.0f
        }, &orbit)) {
        return false;
    }

    orbitState = (ShipOrbitState){
        .active = true,
        .normal = normal,
        .radial = orbit.radial,
        .radius = parkingRadius,
        .gravitationalParameter = mu
    };
    driveMode = SHIP_DRIVE_ORBIT;
    player->position = orbit.position;
    player->velocity = orbit.velocity;
    player->onGround = false;
    if (hasSolidSurface) *hasSolidSurface = profile.hasSolidSurface;
    return true;
}

static bool UpdateNavigationOrbit(Player *player, float dt)
{
    if (!player || !orbitState.active ||
        navigationTarget.type != NAVIGATION_TARGET_PLANET) return false;

    Vector3 center;
    Vector3 centerVelocity;
    if (!ResolveNavigationTarget(SHIP_DRIVE_ORBIT, &center, &centerVelocity,
                                 NULL)) {
        return false;
    }
    ShipCircularOrbitState orbit;
    if (!ShipFlightStepCircularOrbit(&(ShipCircularOrbitInput){
            .center = center,
            .centerVelocity = centerVelocity,
            .position = Vector3Add(
                center, Vector3Scale(orbitState.radial, orbitState.radius)),
            .normal = orbitState.normal,
            .gravitationalParameter = orbitState.gravitationalParameter,
            .radius = orbitState.radius,
            .dt = dt
        }, &orbit)) {
        return false;
    }
    orbitState.radial = orbit.radial;
    player->position = orbit.position;
    player->velocity = orbit.velocity;
    player->onGround = false;
    return true;
}

static bool ResolveNavigationTarget(ShipDriveMode mode, Vector3 *center,
                                    Vector3 *velocity, float *safeDistance)
{
    if (!navigationTarget.locked) return false;

    SolarSystemDef system;
    if (!StarSystemAt(navigationTarget.systemAnchorX,
                      navigationTarget.systemAnchorZ, &system)) {
        return false;
    }

    if (navigationTarget.type == NAVIGATION_TARGET_SYSTEM) {
        if (center) *center = system.center;
        if (velocity) *velocity = Vector3Zero();
        if (safeDistance) {
            *safeDistance = SolarSystemParkingRadiusGame(&system);
        }
        return true;
    }

    if (navigationTarget.type != NAVIGATION_TARGET_PLANET) return false;
    int planetIndex = NavigationPlanetIndexForSystem(&system);
    if (planetIndex < 0 || planetIndex >= system.planetCount) return false;

    SolarPlanetOrbitalState state;
    if (!SolarSystemPlanetStateAtTime(
            &system, planetIndex, SpaceSimulationTime(), &state)) {
        return false;
    }
    if (center) *center = state.center;
    if (velocity) *velocity = state.velocity;
    if (safeDistance) {
        *safeDistance = NavigationPlanetDistance(&system, planetIndex, mode);
    }
    return true;
}

static int PreferredSystemArrivalPlanet(const SolarSystemDef *system)
{
    if (!system) return -1;

    int largestPlanet = -1;
    int largestSolidPlanet = -1;
    double largestRadiusKm = 0.0;
    double largestSolidRadiusKm = 0.0;
    for (int i = 0; i < system->planetCount; i++) {
        double radiusKm = system->planets[i].physicalRadiusKm;
        if (!(radiusKm > 0.0) || !isfinite(radiusKm)) continue;
        if (radiusKm > largestRadiusKm) {
            largestRadiusKm = radiusKm;
            largestPlanet = i;
        }

        PlanetProfile profile = SolarPlanetProfile(system, i);
        if (profile.hasSolidSurface && radiusKm > largestSolidRadiusKm) {
            largestSolidRadiusKm = radiusKm;
            largestSolidPlanet = i;
        }
    }
    return largestSolidPlanet >= 0 ? largestSolidPlanet : largestPlanet;
}

static bool LockNavigationTarget(const Player *player, Vector3 forward)
{
    if (WorldIsSurfaceActive()) {
        SetImportMessage("Launch into space before locking a navigation target.");
        return false;
    }

    SpaceBodyInfo body;
    if (!SpacePlanetNavigationPick(player->position, forward, &body)) {
        SetImportMessage("Move a planet marker near the crosshair to lock it.");
        return false;
    }

    SolarSystemDef currentSystem;
    bool inTargetSystem = FindNearestSystem(
        player->position, SOLAR_SYSTEM_QUERY_RADIUS, &currentSystem, NULL) &&
        currentSystem.anchorX == body.systemAnchorX &&
        currentSystem.anchorZ == body.systemAnchorZ;

    if (driveMode == SHIP_DRIVE_ORBIT) {
        driveMode = SHIP_DRIVE_MANEUVER;
        ClearOrbitState();
    }
    navigationTarget.locked = true;
    navigationTarget.type = NAVIGATION_TARGET_PLANET;
    navigationIntent = inTargetSystem ? NAVIGATION_INTENT_CONTEXTUAL :
                                        NAVIGATION_INTENT_INTERSTELLAR;
    navigationTarget.systemAnchorX = body.systemAnchorX;
    navigationTarget.systemAnchorZ = body.systemAnchorZ;
    navigationTarget.bodyId = body.bodyId;
    navigationTarget.planetIndex = body.index - 1;
    snprintf(navigationTarget.name, sizeof(navigationTarget.name), "%s",
             body.name);
    if (driveMode == SHIP_DRIVE_APPROACH) driveMode = SHIP_DRIVE_MANEUVER;
    SetImportMessage(TextFormat(
        "Locked %s. Press G to engage navigation.", navigationTarget.name));
    return true;
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
    if (fuel <= 0.0f) {
        SetImportMessage("System warp unavailable: ship is out of fuel.");
        return false;
    }

    SolarSystemDef system;
    if (!StarSystemAt(systemAnchorX, systemAnchorZ, &system)) {
        SetImportMessage("Selected star system is unavailable.");
        return false;
    }

    int planetIndex = PreferredSystemArrivalPlanet(&system);
    Vector3 targetCenter = system.center;
    float safeDistance = SolarSystemParkingRadiusGame(&system);
    if (planetIndex >= 0) {
        SolarPlanetOrbitalState state;
        if (!SolarSystemPlanetStateAtTime(
                &system, planetIndex, SpaceSimulationTime(), &state)) {
            planetIndex = -1;
        } else {
            targetCenter = state.center;
            safeDistance = SolarSystemPlanetParkingRadiusGame(
                &system, planetIndex);
        }
    }

    float gap = Vector3Distance(player->position, targetCenter) - safeDistance;
    if (gap <= 1.0f) {
        player->velocity = Vector3Zero();
        SetImportMessage(planetIndex >= 0
            ? TextFormat("Already within %s's approach zone.",
                         system.planets[planetIndex].name)
            : "Already within this star system's approach zone.");
        return false;
    }

    navigationTarget.locked = true;
    navigationTarget.type = planetIndex >= 0 ? NAVIGATION_TARGET_PLANET :
                                               NAVIGATION_TARGET_SYSTEM;
    navigationIntent = NAVIGATION_INTENT_INTERSTELLAR;
    navigationTarget.systemAnchorX = systemAnchorX;
    navigationTarget.systemAnchorZ = systemAnchorZ;
    navigationTarget.bodyId = planetIndex >= 0
        ? system.planets[planetIndex].bodyId : 0u;
    navigationTarget.planetIndex = planetIndex;
    snprintf(navigationTarget.name, sizeof(navigationTarget.name), "%s",
             planetIndex >= 0 && system.planets[planetIndex].name[0]
                 ? system.planets[planetIndex].name : system.name);
    ClearOrbitState();
    driveMode = SHIP_DRIVE_INTERSTELLAR_WARP;
    cruiseSetSpeed = 0.0f;
    player->velocity = Vector3Zero();
    SetImportMessage(planetIndex >= 0
        ? TextFormat("Interstellar warp engaged: %s in %s.",
                     navigationTarget.name, system.name)
        : TextFormat("Interstellar warp engaged: %s.",
                     navigationTarget.name));
    return true;
}

void ShipToggleNavigation(Player *player)
{
    if (!player) return;
    if (driveMode == SHIP_DRIVE_ORBIT) {
        ClearNavigationTarget();
        SetImportMessage("Orbit disengaged. Manual maneuvering active.");
        return;
    }
    if (ShipDriveIsGuided()) {
        bool highSpeed = ShipDriveIsWarpTransit();
        bool interstellar = driveMode == SHIP_DRIVE_INTERSTELLAR_WARP;
        driveMode = SHIP_DRIVE_MANEUVER;
        if (highSpeed) player->velocity = Vector3Zero();
        SetImportMessage(interstellar ? "Interstellar warp cancelled." :
                         highSpeed ? "Supercruise cancelled." :
                                     "Approach guidance cancelled.");
        return;
    }
    if (!driving) {
        SetImportMessage("Board your ship before engaging navigation.");
        return;
    }
    if (WorldIsSurfaceActive()) {
        SetImportMessage("Launch into space before engaging navigation.");
        return;
    }
    if (fuel <= 0.0f) {
        SetImportMessage("Navigation unavailable: ship is out of fuel.");
        return;
    }

    Vector3 targetCenter;
    Vector3 targetVelocity;
    float safeDistance = 0.0f;
    if (!ResolveNavigationTarget(driveMode, &targetCenter, &targetVelocity,
                                 &safeDistance)) {
        ClearNavigationTarget();
        SetImportMessage("Navigation target is no longer available.");
        return;
    }

    float gap = Vector3Distance(player->position, targetCenter) - safeDistance;
    if (gap <= NavigationArrivalTolerance(safeDistance)) {
        if (navigationTarget.type == NAVIGATION_TARGET_PLANET) {
            SolarSystemDef system;
            if (StarSystemAt(navigationTarget.systemAnchorX,
                             navigationTarget.systemAnchorZ, &system)) {
                int planetIndex = NavigationPlanetIndexForSystem(&system);
                bool hasSolidSurface = false;
                if (EstablishNavigationOrbit(
                        player, &system, planetIndex, targetCenter,
                        targetVelocity, safeDistance, &hasSolidSurface)) {
                    SetImportMessage(hasSolidSurface
                        ? TextFormat(
                            "Orbit established around %s. Press E to land.",
                            navigationTarget.name)
                        : TextFormat(
                            "Orbit established around %s. No solid surface available.",
                            navigationTarget.name));
                    return;
                }
            }
        }
        player->velocity = Vector3Zero();
        SetImportMessage("Already at the target approach distance.");
        return;
    }

    cruiseSetSpeed = 0.0f;
    ClearOrbitState();
    ShipNavigationRoute route = ShipNavigationSelectRoute(
        &(ShipNavigationRouteInput){
            .gap = gap,
            .safeDistance = safeDistance,
            .approachSpeed = SHIP_CRUISE_MAX_SPEED,
            .interstellar = navigationIntent == NAVIGATION_INTENT_INTERSTELLAR
        });
    switch (route) {
    case SHIP_NAVIGATION_INTERSTELLAR_WARP:
        driveMode = SHIP_DRIVE_INTERSTELLAR_WARP;
        SetImportMessage(TextFormat("Interstellar warp engaged: %s.",
                                    navigationTarget.name));
        break;
    case SHIP_NAVIGATION_SUPERCRUISE:
        driveMode = SHIP_DRIVE_SUPERCRUISE;
        SetImportMessage(TextFormat("Supercruise engaged: %s.",
                                    navigationTarget.name));
        break;
    default:
        driveMode = SHIP_DRIVE_APPROACH;
        SetImportMessage(TextFormat("Approach guidance engaged: %s.",
                                    navigationTarget.name));
        break;
    }
}

static void ShipBeginFlight(Player *player)
{
    player->velocity = Vector3Zero();
    player->floating = true;
    driving = true;
    driveMode = SHIP_DRIVE_MANEUVER;
    cruiseSetSpeed = 0.0f;
    relativeSpeed = 0.0f;
    gravityPrimary = (SpaceGravitySample){ 0 };
    ClearNavigationTarget();
    ShipResetVisualEffects();
    SetImportMessage("Ship: inertial flight. F toggles braking assist; E exits.");
}

bool ShipTryEnter(int x, int y, int z, Player *player)
{
    ParkedShip ship;
    if (driving || !player || !ShipResolveParkedAt(x, y, z, &ship)) return false;
    if (!ShipRemoveParkedAt(x, y, z, false)) return false;

    if (ship.legacy) {
        player->position = (Vector3){
            (float)ship.coreX + 0.5f, (float)ship.coreY + 0.5f,
            (float)ship.coreZ + 0.5f
        };
    } else {
        player->position = (Vector3){
            (float)ship.coreX + 1.0f, (float)ship.coreY + 0.5f,
            (float)ship.coreZ + 1.0f
        };
        player->yaw = (float)ship.direction * PI * 0.5f;
    }
    ShipBeginFlight(player);
    return true;
}

bool ShipBeginDebugFlight(Player *player)
{
    if (driving || !player) return false;
    ShipBeginFlight(player);
    return true;
}

bool ShipIsDriving(void)
{
    return driving;
}

bool ShipIsCruising(void)
{
    return driveMode == SHIP_DRIVE_MANUAL_CRUISE ||
           driveMode == SHIP_DRIVE_APPROACH ||
           driveMode == SHIP_DRIVE_SUPERCRUISE;
}

bool ShipIsOrbiting(void)
{
    return driveMode == SHIP_DRIVE_ORBIT;
}

bool ShipIsApproaching(void)
{
    return driveMode == SHIP_DRIVE_APPROACH;
}

bool ShipIsSupercruising(void)
{
    return driveMode == SHIP_DRIVE_SUPERCRUISE;
}

bool ShipIsInterstellarWarping(void)
{
    return driveMode == SHIP_DRIVE_INTERSTELLAR_WARP;
}

bool ShipIsHighSpeedTransit(void)
{
    return ShipDriveIsWarpTransit();
}

ShipDriveMode ShipGetDriveMode(void)
{
    return driveMode;
}

const char *ShipDriveModeName(void)
{
    switch (driveMode) {
    case SHIP_DRIVE_ORBIT: return "ORBIT";
    case SHIP_DRIVE_MANUAL_CRUISE: return "MANUAL CRUISE";
    case SHIP_DRIVE_APPROACH: return "APPROACH";
    case SHIP_DRIVE_SUPERCRUISE: return "SUPERCRUISE";
    case SHIP_DRIVE_INTERSTELLAR_WARP: return "INTERSTELLAR WARP";
    default: return flightAssist ? "MANEUVER ASSIST" : "MANEUVER";
    }
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

bool ShipHasNavigationTarget(void)
{
    return navigationTarget.locked;
}

bool ShipNavigationTargetIsSystem(void)
{
    return navigationTarget.locked &&
           navigationTarget.type == NAVIGATION_TARGET_SYSTEM;
}

const char *ShipNavigationTargetName(void)
{
    return navigationTarget.locked ? navigationTarget.name : "---";
}

float ShipRelativeSpeed(void)
{
    return relativeSpeed;
}

float ShipTargetSpeed(void)
{
    return targetSpeed;
}

float ShipTargetClosingSpeed(void)
{
    return targetClosingSpeed;
}

float ShipTargetBrakingDistance(void)
{
    return targetBrakingDistance;
}

float ShipTargetEtaSeconds(void)
{
    return targetEtaSeconds;
}

float ShipInterstellarWarpVisualIntensity(void)
{
    if (driveMode != SHIP_DRIVE_INTERSTELLAR_WARP) return 0.0f;
    float speedRatio = Clamp(relativeSpeed / SHIP_INTERSTELLAR_MAX_SPEED,
                             0.0f, 1.0f);
    return 0.30f + 0.70f * sqrtf(speedRatio);
}

float ShipDriveTunnelIntensity(void)
{
    float warpIntensity = ShipInterstellarWarpVisualIntensity();
    if (warpIntensity > 0.0f) return warpIntensity;
    if (driveMode != SHIP_DRIVE_SUPERCRUISE) return 0.0f;

    float commandedSpeed = fmaxf(targetSpeed, relativeSpeed);
    float speedRatio = Clamp(commandedSpeed / SHIP_SUPERCRUISE_MAX_SPEED,
                             0.0f, 1.0f);
    return 0.16f + 0.52f * sqrtf(speedRatio);
}

void ShipReset(void)
{
    driving = false;
    driveMode = SHIP_DRIVE_MANEUVER;
    flightAssist = false;
    cruiseSetSpeed = 0.0f;
    relativeSpeed = 0.0f;
    gravityPrimary = (SpaceGravitySample){ 0 };
    ClearNavigationTarget();
    fuel = SHIP_MAX_FUEL;
    ShipResetVisualEffects();
}

float ShipGetFuel(void)
{
    return fuel;
}

bool ShipConsumeFuel(float amount)
{
    if (!isfinite(amount) || amount < 0.0f ||
        !isfinite(fuel) || fuel < 0.0f || fuel > SHIP_MAX_FUEL) {
        return false;
    }
    if (amount == 0.0f) return true;
    if (amount > fuel) {
        fuel = 0.0f;
        return false;
    }
    fuel -= amount;
    if (fuel < 0.000001f) fuel = 0.0f;
    return true;
}

bool ShipRefuel(void)
{
    fuel = SHIP_MAX_FUEL;
    SetImportMessage("Ship fuel restored to maximum.");
    return true;
}

bool ShipSaveState(FILE *file)
{
    if (!file || !isfinite(fuel) || fuel < 0.0f || fuel > SHIP_MAX_FUEL) {
        return false;
    }
    return fwrite(&fuel, sizeof(fuel), 1, file) == 1;
}

bool ShipLoadState(FILE *file)
{
    float loadedFuel = 0.0f;
    if (!file || fread(&loadedFuel, sizeof(loadedFuel), 1, file) != 1 ||
        !isfinite(loadedFuel) || loadedFuel < 0.0f || loadedFuel > SHIP_MAX_FUEL) {
        return false;
    }
    ShipReset();
    fuel = loadedFuel;
    return true;
}

void ShipToggleCruise(void)
{
    if (driveMode == SHIP_DRIVE_ORBIT) {
        driveMode = SHIP_DRIVE_MANEUVER;
        ClearOrbitState();
        SetImportMessage("Orbit disengaged. Manual maneuvering active.");
        return;
    }
    if (ShipDriveIsGuided() || !driving || fuel <= 0.0f) return;
    if (driveMode == SHIP_DRIVE_MANUAL_CRUISE) {
        driveMode = SHIP_DRIVE_MANEUVER;
        cruiseSetSpeed = 0.0f;
        SetImportMessage("Manual cruise off.");
        return;
    }
    ClearOrbitState();
    driveMode = SHIP_DRIVE_MANUAL_CRUISE;
    cruiseSetSpeed = fmaxf(cruiseSetSpeed,
                           SHIP_MANUAL_CRUISE_INITIAL_SPEED);
    SetImportMessage("Manual cruise: W/S set speed, X returns to maneuver.");
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

bool ShipSetDebugExhaust(const Player *player, float demand)
{
    if (!player || !isfinite(demand) || demand < 0.0f || demand > 1.0f) {
        return false;
    }
    ShipVisualSetDebugExhaust(player, demand,
                              ShipAtmosphereDensityAt(player->position));
    return true;
}

typedef struct ShipFrameControl {
    bool scripted;
    float forward;
    float strafe;
    float vertical;
} ShipFrameControl;

typedef struct ShipFrameEnvironment {
    bool hasGravity;
    SpaceGravitySample gravity;
    Vector3 referenceVelocity;
    Vector3 vertical;
} ShipFrameEnvironment;

typedef struct ShipFrameState {
    ShipFrameControl control;
    ShipFrameEnvironment environment;
    Vector3 forward;
    Vector3 acceleration;
    bool surfaceActive;
    bool automaticDrive;
    bool translationInput;
} ShipFrameState;

static ShipFrameControl ShipReadFrameControl(const ShipControlInput *input)
{
    ShipFrameControl control = { .scripted = input != NULL };
    if (control.scripted) {
        control.forward = isfinite(input->forward)
            ? Clamp(input->forward, -1.0f, 1.0f) : 0.0f;
        control.strafe = isfinite(input->strafe)
            ? Clamp(input->strafe, -1.0f, 1.0f) : 0.0f;
        control.vertical = isfinite(input->vertical)
            ? Clamp(input->vertical, -1.0f, 1.0f) : 0.0f;
        return control;
    }
    control.forward = (IsKeyDown(KEY_W) ? 1.0f : 0.0f) -
                      (IsKeyDown(KEY_S) ? 1.0f : 0.0f);
    control.strafe = (IsKeyDown(KEY_D) ? 1.0f : 0.0f) -
                     (IsKeyDown(KEY_A) ? 1.0f : 0.0f);
    control.vertical = (IsKeyDown(KEY_SPACE) ? 1.0f : 0.0f) -
                       (IsKeyDown(KEY_LEFT_CONTROL) ? 1.0f : 0.0f);
    return control;
}

static Vector3 ShipApplyFrameCommands(Player *player, bool scripted,
                                      bool surfaceActive)
{
    if (!scripted && IsKeyPressed(KEY_X)) {
        if (ShipDriveIsGuided()) {
            SetImportMessage("Navigation active. Press G to cancel it.");
        } else if (surfaceActive) {
            SetImportMessage("Launch into space before engaging cruise.");
        } else {
            ShipToggleCruise();
        }
    }
    if (!scripted && IsKeyPressed(KEY_R)) ShipRefuel();
    if (!scripted && IsKeyPressed(KEY_F) && !ShipDriveIsGuided()) {
        flightAssist = !flightAssist;
        SetImportMessage(flightAssist ?
                         "Flight assist enabled: releasing thrust brakes the ship." :
                         "Flight assist disabled: inertial flight active.");
    }

    Vector2 mouseDelta = scripted ? Vector2Zero() : GetMouseDelta();
    if (!ShipDriveIsGuided()) {
        player->yaw -= mouseDelta.x * MOUSE_SENSITIVITY;
        player->pitch -= mouseDelta.y * MOUSE_SENSITIVITY;
    }
    player->pitch = Clamp(player->pitch, -1.45f, 1.45f);

    Vector3 forward = ForwardFromAngles(player->yaw, player->pitch);
    if (!scripted && IsKeyPressed(KEY_Q)) {
        if (ShipDriveIsWarpTransit()) {
            SetImportMessage("Cancel high-speed navigation before changing target.");
        } else {
            LockNavigationTarget(player, forward);
        }
    }
    if (!scripted && IsKeyPressed(KEY_G)) {
        if (!navigationTarget.locked && !ShipDriveIsGuided()) {
            SetImportMessage("Lock a navigation target with Q first.");
        } else {
            ShipToggleNavigation(player);
        }
    }
    return forward;
}

static ShipFrameEnvironment ShipFrameEnvironmentAt(const Player *player)
{
    ShipFrameEnvironment environment = {
        .referenceVelocity = Vector3Zero(),
        .vertical = { 0.0f, 1.0f, 0.0f }
    };
    Vector3 planetDirection = Vector3Zero();
    float surfaceDistance = 0.0f;
    bool nearPlanet = WorldCurrentDimension() != WORLD_DIMENSION_PLANET &&
        PlanetSurfaceAt(player->position, &planetDirection, &surfaceDistance,
                        NULL);
    environment.hasGravity = WorldIsSpaceActive() &&
        SpaceGravityAt(player->position, &environment.gravity);
    gravityPrimary = environment.hasGravity ? environment.gravity :
                                               (SpaceGravitySample){ 0 };
    if (environment.hasGravity) {
        environment.referenceVelocity = environment.gravity.primaryVelocity;
    }
    if (nearPlanet) {
        environment.vertical = Vector3Negate(planetDirection);
    } else if (environment.hasGravity &&
               (environment.gravity.kind == SPACE_GRAVITY_PRIMARY_PLANET ||
                environment.gravity.kind == SPACE_GRAVITY_PRIMARY_HOME)) {
        environment.vertical = Vector3Normalize(Vector3Subtract(
            player->position, environment.gravity.center));
    }
    return environment;
}

static bool ShipConsumeFrameFuel(Player *player, float dt,
                                 bool propulsionRequested)
{
    if (!propulsionRequested) return true;
    float fuelRate = SHIP_THRUST_FUEL_RATE;
    if (driveMode == SHIP_DRIVE_INTERSTELLAR_WARP) {
        fuelRate = SHIP_INTERSTELLAR_FUEL_RATE;
    } else if (driveMode == SHIP_DRIVE_SUPERCRUISE) {
        fuelRate = SHIP_SUPERCRUISE_FUEL_RATE;
    } else if (driveMode == SHIP_DRIVE_APPROACH ||
               driveMode == SHIP_DRIVE_MANUAL_CRUISE) {
        fuelRate = SHIP_CRUISE_FUEL_RATE;
    }
    if (ShipConsumeFuel(fuelRate * dt)) return true;

    bool haltedHighSpeedTransit = ShipDriveIsWarpTransit();
    driveMode = SHIP_DRIVE_MANEUVER;
    cruiseSetSpeed = 0.0f;
    if (haltedHighSpeedTransit) player->velocity = Vector3Zero();
    SetImportMessage("Propulsion disabled: ship is out of fuel.");
    return false;
}

static void ShipMoveForFrame(Player *player, float dt,
                             bool positionControlledByDrive)
{
    Vector3 delta = Vector3Scale(player->velocity, dt);
    if (positionControlledByDrive) {
        player->onGround = false;
        return;
    }
    if (ShipDriveIsWarpTransit()) {
        Vector3 nextPosition = Vector3Add(player->position, delta);
        if (isfinite(nextPosition.x) && isfinite(nextPosition.y) &&
            isfinite(nextPosition.z)) {
            player->position = nextPosition;
            player->onGround = false;
        } else {
            player->velocity = Vector3Zero();
            driveMode = SHIP_DRIVE_MANEUVER;
            SetImportMessage("Transit halted: navigation solution became invalid.");
        }
        return;
    }

    float total = Vector3Length(delta);
    if (total <= 0.001f) return;
    Vector3 direction = Vector3Scale(delta, 1.0f / total);
    float remaining = total;
    while (remaining > 0.0f) {
        float stepLength = fminf(remaining, 1.0f);
        Vector3 before = player->position;
        MovePlayer(player, Vector3Scale(direction, stepLength));
        if (Vector3Distance(before, player->position) < stepLength * 0.9f) {
            player->velocity = Vector3Zero();
            break;
        }
        remaining -= stepLength;
    }
}

static void ShipCorrectPlanetCollision(Player *player)
{
    if (driveMode == SHIP_DRIVE_ORBIT ||
        WorldCurrentDimension() == WORLD_DIMENSION_PLANET) return;

    Vector3 correctionDirection = Vector3Zero();
    float surfaceDistance = 0.0f;
    if (!PlanetSurfaceAt(player->position, &correctionDirection,
                         &surfaceDistance, NULL) ||
        surfaceDistance >= SHIP_SPACE_COLLISION_CLEARANCE) return;
    player->position = Vector3Add(
        player->position,
        Vector3Scale(Vector3Negate(correctionDirection),
                     SHIP_SPACE_COLLISION_CLEARANCE - surfaceDistance));
    float inwardSpeed = Vector3DotProduct(player->velocity,
                                          correctionDirection);
    if (inwardSpeed > 0.0f) {
        player->velocity = Vector3Subtract(
            player->velocity,
            Vector3Scale(correctionDirection, inwardSpeed));
    }
}

static void ShipUpdateFrameEffects(const Player *player, float dt,
                                   bool surfaceActive, bool automaticDrive,
                                   float forwardInput, float verticalInput,
                                   float atmosphereDensity)
{
    bool manualForward = !automaticDrive && forwardInput > 0.001f;
    bool positiveCruise = driveMode == SHIP_DRIVE_MANUAL_CRUISE &&
                          cruiseSetSpeed > 0.001f;
    bool poweredDrive = driveMode == SHIP_DRIVE_APPROACH ||
                        driveMode == SHIP_DRIVE_SUPERCRUISE ||
                        driveMode == SHIP_DRIVE_INTERSTELLAR_WARP ||
                        positiveCruise;
    float exhaustDemand = 0.0f;
    if (fuel > 0.0f && (manualForward || poweredDrive)) {
        switch (driveMode) {
        case SHIP_DRIVE_MANUAL_CRUISE: exhaustDemand = 0.80f; break;
        case SHIP_DRIVE_APPROACH: exhaustDemand = 0.76f; break;
        case SHIP_DRIVE_SUPERCRUISE: exhaustDemand = 0.94f; break;
        case SHIP_DRIVE_INTERSTELLAR_WARP: exhaustDemand = 1.0f; break;
        default: exhaustDemand = 0.65f; break;
        }
    }
    ShipVisualUpdateMainExhaust(player, dt, driveMode, exhaustDemand,
                                atmosphereDensity);
    if (surfaceActive && fuel > 0.0f && !automaticDrive &&
        verticalInput > 0.001f) {
        ShipGroundEffectsEmit(player, dt, 1.0f, false);
    }
}

static ShipFrameState ShipPrepareFrameState(
    Player *player, const ShipControlInput *input)
{
    ShipFrameState frame = { 0 };
    frame.control = ShipReadFrameControl(input);
    frame.surfaceActive = WorldIsSurfaceActive();
    frame.forward = ShipApplyFrameCommands(
        player, frame.control.scripted, frame.surfaceActive);
    frame.automaticDrive = ShipDriveIsGuided();
    frame.environment = ShipFrameEnvironmentAt(player);

    if (!frame.automaticDrive &&
        driveMode != SHIP_DRIVE_MANUAL_CRUISE) {
        frame.acceleration = Vector3Scale(
            frame.forward, frame.control.forward);
    }
    if (!frame.automaticDrive) {
        Vector3 right = RightFromYaw(player->yaw);
        frame.acceleration = Vector3Add(
            frame.acceleration, Vector3Scale(right, frame.control.strafe));
        frame.acceleration = Vector3Add(
            frame.acceleration,
            Vector3Scale(frame.environment.vertical, frame.control.vertical));
    }
    frame.translationInput =
        Vector3LengthSqr(frame.acceleration) > 0.0f;
    if (frame.translationInput) {
        frame.acceleration = Vector3Normalize(frame.acceleration);
    }
    if (frame.translationInput && driveMode == SHIP_DRIVE_ORBIT) {
        driveMode = SHIP_DRIVE_MANEUVER;
        ClearOrbitState();
        SetImportMessage("Orbit disengaged by manual thrust.");
    }
    return frame;
}

static void ShipUpdateManualCruiseSetSpeed(
    const ShipFrameState *frame, float dt)
{
    if (driveMode != SHIP_DRIVE_MANUAL_CRUISE || dt <= 0.0f) return;
    cruiseSetSpeed += frame->control.forward *
                      SHIP_CRUISE_SPEED_CHANGE * dt;
    cruiseSetSpeed = Clamp(cruiseSetSpeed,
                           -SHIP_MANUAL_CRUISE_REVERSE_SPEED,
                           SHIP_CRUISE_MAX_SPEED);
}

static void ShipApplyFrameGravity(Player *player, float dt,
                                  const ShipFrameState *frame)
{
    if (dt <= 0.0f || ShipDriveIsWarpTransit() ||
        driveMode == SHIP_DRIVE_ORBIT || !frame->environment.hasGravity) {
        return;
    }
    player->velocity = Vector3Add(
        player->velocity,
        Vector3Scale(frame->environment.gravity.acceleration, dt));
}

static void ShipGuidanceLimits(ShipDriveMode mode, float *maxSpeed,
                               float *acceleration, float *deceleration)
{
    *maxSpeed = SHIP_CRUISE_MAX_SPEED;
    *acceleration = SHIP_CRUISE_ACCEL;
    *deceleration = SHIP_CRUISE_DECEL;
    if (mode == SHIP_DRIVE_SUPERCRUISE) {
        *maxSpeed = SHIP_SUPERCRUISE_MAX_SPEED;
        *acceleration = SHIP_SUPERCRUISE_ACCEL;
        *deceleration = SHIP_SUPERCRUISE_DECEL;
    } else if (mode == SHIP_DRIVE_INTERSTELLAR_WARP) {
        *maxSpeed = SHIP_INTERSTELLAR_MAX_SPEED;
        *acceleration = SHIP_INTERSTELLAR_ACCEL;
        *deceleration = SHIP_INTERSTELLAR_DECEL;
    }
}

static bool ShipCompleteGuidance(
    Player *player, ShipDriveMode guidanceMode, Vector3 targetCenter,
    Vector3 targetVelocity, float safeDistance)
{
    char targetName[sizeof(navigationTarget.name)];
    snprintf(targetName, sizeof(targetName), "%s", navigationTarget.name);
    bool targetIsSystem = navigationTarget.type == NAVIGATION_TARGET_SYSTEM;
    if (guidanceMode == SHIP_DRIVE_SUPERCRUISE) {
        driveMode = SHIP_DRIVE_APPROACH;
        player->velocity = targetVelocity;
        SetImportMessage(TextFormat(
            "Supercruise complete. Approach guidance engaged: %s.",
            targetName));
        return false;
    }
    if (guidanceMode == SHIP_DRIVE_INTERSTELLAR_WARP) {
        player->velocity = targetVelocity;
        if (targetIsSystem) {
            ClearNavigationTarget();
            SetImportMessage(TextFormat("Arrived in %s.", targetName));
        } else {
            navigationIntent = NAVIGATION_INTENT_CONTEXTUAL;
            driveMode = SHIP_DRIVE_SUPERCRUISE;
            SetImportMessage(TextFormat(
                "Warp complete. Supercruise engaged: %s.", targetName));
        }
        return false;
    }

    bool orbitEstablished = false;
    bool hasSolidSurface = false;
    if (!targetIsSystem) {
        SolarSystemDef arrivalSystem;
        if (StarSystemAt(navigationTarget.systemAnchorX,
                         navigationTarget.systemAnchorZ, &arrivalSystem)) {
            int planetIndex = NavigationPlanetIndexForSystem(&arrivalSystem);
            orbitEstablished = EstablishNavigationOrbit(
                player, &arrivalSystem, planetIndex, targetCenter,
                targetVelocity, safeDistance, &hasSolidSurface);
        }
    }
    if (!orbitEstablished) {
        driveMode = SHIP_DRIVE_MANEUVER;
        ClearOrbitState();
        player->velocity = targetVelocity;
    }
    targetSpeed = 0.0f;
    targetClosingSpeed = 0.0f;
    targetBrakingDistance = 0.0f;
    targetEtaSeconds = 0.0f;
    if (orbitEstablished) {
        SetImportMessage(hasSolidSurface
            ? TextFormat("Orbit established around %s. Press E to land.",
                         targetName)
            : TextFormat(
                "Orbit established around %s. No solid surface available.",
                targetName));
    } else {
        SetImportMessage(TextFormat(
            "Matched %s approach velocity.", targetName));
    }
    return orbitEstablished;
}

static bool ShipUpdateGuidedDrive(Player *player, float dt,
                                  Vector3 *forward)
{
    Vector3 targetCenter;
    Vector3 targetVelocity;
    float safeDistance = 0.0f;
    ShipDriveMode guidanceMode = driveMode;
    bool highSpeedTransit = ShipDriveIsWarpTransit();
    if (!ResolveNavigationTarget(guidanceMode, &targetCenter,
                                 &targetVelocity, &safeDistance)) {
        ClearNavigationTarget();
        if (highSpeedTransit) player->velocity = Vector3Zero();
        SetImportMessage("Navigation target is no longer available.");
        return false;
    }

    float maxSpeed;
    float acceleration;
    float deceleration;
    ShipGuidanceLimits(guidanceMode, &maxSpeed, &acceleration,
                       &deceleration);
    ShipFlightGuidanceInput guidanceInput = {
        .position = player->position,
        .velocity = player->velocity,
        .targetPosition = targetCenter,
        .targetVelocity = targetVelocity,
        .safeDistance = safeDistance,
        .arrivalTolerance = NavigationArrivalTolerance(safeDistance),
        .maxSpeed = maxSpeed,
        .acceleration = acceleration,
        .deceleration = deceleration,
        .dt = dt
    };
    ShipFlightGuidance guidance;
    if (!ShipFlightGuideToTarget(&guidanceInput, &guidance)) {
        ClearNavigationTarget();
        if (highSpeedTransit) player->velocity = Vector3Zero();
        SetImportMessage("Navigation guidance failed.");
        return false;
    }

    targetSpeed = guidance.desiredSpeed;
    targetClosingSpeed = guidance.closingSpeed;
    targetBrakingDistance = guidance.brakingDistance;
    targetEtaSeconds = guidance.etaSeconds;
    player->velocity = guidance.velocity;
    if (Vector3LengthSqr(guidance.direction) > 0.0f) {
        *forward = guidance.direction;
        player->yaw = atan2f(forward->x, forward->z);
        player->pitch = asinf(Clamp(forward->y, -1.0f, 1.0f));
    }
    return guidance.arrived && ShipCompleteGuidance(
        player, guidanceMode, targetCenter, targetVelocity, safeDistance);
}

static void ShipUpdateManualCruiseDrive(
    Player *player, float dt, const ShipFrameState *frame)
{
    Vector3 relative = Vector3Subtract(
        player->velocity, frame->environment.referenceVelocity);
    Vector3 desired = Vector3Scale(frame->forward, cruiseSetSpeed);
    if (frame->translationInput) {
        desired = Vector3Add(
            desired, Vector3Scale(frame->acceleration, 4.0f));
    }
    relative = ShipFlightApproachVelocity(
        relative, desired, SHIP_CRUISE_ACCEL, SHIP_CRUISE_DECEL, dt);
    player->velocity = Vector3Add(
        frame->environment.referenceVelocity, relative);
    targetSpeed = fabsf(cruiseSetSpeed);
    targetClosingSpeed = 0.0f;
    targetBrakingDistance = Vector3LengthSqr(relative) /
                            (2.0f * SHIP_CRUISE_DECEL);
    targetEtaSeconds = 0.0f;
}

static void ShipUpdateManeuverDrive(Player *player, float dt,
                                    const ShipFrameState *frame)
{
    float thrust = frame->surfaceActive ? SHIP_SURFACE_THRUST :
                                          SHIP_SPACE_MANEUVER_THRUST;
    player->velocity = Vector3Add(
        player->velocity, Vector3Scale(frame->acceleration, thrust * dt));
    if (!frame->surfaceActive) {
        player->velocity = ShipFlightClampRelativeVelocity(
            player->velocity, frame->environment.referenceVelocity,
            SHIP_SPACE_MANEUVER_MAX_SPEED);
    }
    if (flightAssist && !frame->translationInput) {
        Vector3 relative = Vector3Subtract(
            player->velocity, frame->environment.referenceVelocity);
        relative = SpacePhysicsBrakeVelocity(
            relative, frame->surfaceActive ? SHIP_SURFACE_ASSIST_DECEL :
                                             SHIP_SPACE_ASSIST_DECEL,
            dt);
        player->velocity = Vector3Add(
            frame->environment.referenceVelocity, relative);
    }
    targetSpeed = 0.0f;
    targetClosingSpeed = 0.0f;
    targetBrakingDistance = 0.0f;
    targetEtaSeconds = 0.0f;
}

static bool ShipUpdateDriveForFrame(Player *player, float dt,
                                    ShipFrameState *frame)
{
    if (dt <= 0.0f) return false;
    if (ShipDriveIsGuided()) {
        return ShipUpdateGuidedDrive(player, dt, &frame->forward);
    }
    if (driveMode == SHIP_DRIVE_ORBIT) {
        if (UpdateNavigationOrbit(player, dt)) return true;
        ClearNavigationTarget();
        SetImportMessage(
            "Orbit control lost its target. Manual maneuvering active.");
        return false;
    }
    if (driveMode == SHIP_DRIVE_MANUAL_CRUISE) {
        ShipUpdateManualCruiseDrive(player, dt, frame);
        return false;
    }
    ShipUpdateManeuverDrive(player, dt, frame);
    return false;
}

static float ShipApplyFrameAtmosphere(Player *player, float dt,
                                      const ShipFrameState *frame)
{
    float density = ShipAtmosphereDensityAt(player->position);
    if (dt > 0.0f && density > 0.0f && !ShipDriveIsWarpTransit()) {
        float drag = expf(-SHIP_ATMOSPHERE_DRAG * density * dt);
        player->velocity = Vector3Scale(player->velocity, drag);
    }
    relativeSpeed = Vector3Length(Vector3Subtract(
        player->velocity, frame->environment.referenceVelocity));
    return density;
}


void ShipUpdate(Player *player, float dt)
{
    ShipUpdateWithInput(player, dt, NULL);
}

void ShipUpdateWithInput(Player *player, float dt,
                         const ShipControlInput *input)
{
    if (!player) return;
    if (!isfinite(dt) || dt <= 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

    ShipFrameState frame = ShipPrepareFrameState(player, input);
    ShipUpdateManualCruiseSetSpeed(&frame, dt);
    bool propulsionRequested = frame.translationInput ||
        (driveMode == SHIP_DRIVE_MANUAL_CRUISE &&
         fabsf(cruiseSetSpeed) > 0.001f) ||
        ShipDriveIsGuided();
    if (!ShipConsumeFrameFuel(player, dt, propulsionRequested)) {
        frame.translationInput = false;
        frame.acceleration = Vector3Zero();
    }

    ShipApplyFrameGravity(player, dt, &frame);
    bool positionControlledByDrive =
        ShipUpdateDriveForFrame(player, dt, &frame);
    float atmosphereDensity =
        ShipApplyFrameAtmosphere(player, dt, &frame);
    ShipMoveForFrame(player, dt, positionControlledByDrive);
    ShipCorrectPlanetCollision(player);
    ShipUpdateFrameEffects(
        player, dt, frame.surfaceActive, frame.automaticDrive,
        frame.control.forward, frame.control.vertical, atmosphereDensity);
}

void ShipUpdateLandingEffects(const Player *player, float dt,
                              float descentProgress)
{
    if (!player || !driving || !isfinite(dt) || dt <= 0.0f) return;
    float progress = Clamp(descentProgress, 0.0f, 1.0f);
    float demand = 0.32f + progress * 0.26f;
    ShipVisualUpdateMainExhaust(player, dt, SHIP_DRIVE_MANEUVER, demand,
                                ShipAtmosphereDensityAt(player->position));
    if (WorldIsSurfaceActive() && progress > 0.55f) {
        ShipGroundEffectsEmit(player, dt,
                              (progress - 0.55f) / 0.45f, false);
    }
}

void ShipEmitTouchdownDust(const Player *player)
{
    if (!player) return;
    ShipGroundEffectsEmit(player, 0.0f, 1.0f, true);
}

static bool FindShipPlacement(int *outX, int *outY, int *outZ,
                              ShipDirection direction, const Player *player)
{
    int centerX = (int)floorf(player->position.x);
    int centerZ = (int)floorf(player->position.z);
    int baseY = (int)floorf(player->position.y);
    WorldBlockRegion region = WorldBlockRegionAt(baseY);

    for (int radius = 0; radius <= 8; radius++) {
        for (int dx = -radius; dx <= radius; dx++) {
            for (int dz = -radius; dz <= radius; dz++) {
                if (radius > 0 && abs(dx) != radius && abs(dz) != radius) continue;
                int coreX = centerX + dx - 1;
                int coreZ = centerZ + dz - 1;
                int minY = region == WORLD_BLOCK_REGION_SURFACE ? baseY - 24 : baseY - 1;
                int maxY = baseY + 3;
                int verticalRange = maxY - minY;
                for (int offset = 0; offset <= verticalRange; offset++) {
                    int candidates[2] = { baseY + offset, baseY - offset };
                    int candidateCount = offset == 0 ? 1 : 2;
                    for (int candidate = 0; candidate < candidateCount; candidate++) {
                        int y = candidates[candidate];
                        if (y < minY || y > maxY ||
                            !ShipCanPlaceParked(coreX, y, coreZ, direction,
                                                player)) continue;
                        *outX = coreX;
                        *outY = y;
                        *outZ = coreZ;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

static bool ShipPlaceAfterExit(Player *player)
{
    int sx = 0;
    int sy = 0;
    int sz = 0;
    ShipDirection direction = ShipDirectionFromYaw(player->yaw);
    if (!FindShipPlacement(&sx, &sy, &sz, direction, player) ||
        !ShipPlaceParked(sx, sy, sz, direction, false)) {
        SetImportMessage("Cannot exit: spaceship needs a clear 4x4 area.");
        return false;
    }
    driving = false;
    driveMode = SHIP_DRIVE_MANEUVER;
    cruiseSetSpeed = 0.0f;
    player->floating = WorldIsSpaceActive();
    player->onGround = false;
    gravityPrimary = (SpaceGravitySample){ 0 };
    ClearNavigationTarget();
    ShipResetVisualEffects();
    return true;
}

bool ShipExit(Player *player)
{
    if (!driving) return true;
    return ShipPlaceAfterExit(player);
}

bool ShipForceExit(Player *player)
{
    if (!driving) return true;
    return ShipPlaceAfterExit(player);
}
