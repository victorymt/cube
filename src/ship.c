#include "ship.h"

#include "raymath.h"
#include "block_atlas.h"
#include "chunks.h"
#include "world.h"
#include "player.h"
#include "particles.h"
#include "space.h"
#include "space_physics.h"
#include "world_environment.h"

#include <math.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define SHIP_THRUST 30.0f
#define SHIP_CRUISE_THRUST_MULTIPLIER 4.0f
#define SHIP_ATMOSPHERE_DRAG 0.65f
#define SHIP_ASSIST_DECEL 18.0f
#define SHIP_WARP_MAX_SPEED 1200.0f
#define SHIP_WARP_ACCEL 320.0f
#define SHIP_WARP_DECEL 1200.0f
#define SHIP_PLANET_WARP_MIN_TOLERANCE 0.00025f
#define SHIP_SPACE_COLLISION_CLEARANCE 0.00025f
#define SHIP_THRUST_FUEL_RATE 0.20f
#define SHIP_CRUISE_FUEL_RATE 0.65f
#define SHIP_WARP_FUEL_RATE 2.50f

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
    uint32_t bodyId;
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

static float WarpArrivalTolerance(float safeDistance)
{
    if (warpTarget.type == WARP_TARGET_SYSTEM) return 1.0f;
    return fmaxf(safeDistance * 0.05f,
                 SHIP_PLANET_WARP_MIN_TOLERANCE);
}

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
            *safeDistance = SolarSystemParkingRadiusGame(&system);
        }
        return true;
    }

    if (warpTarget.type != WARP_TARGET_PLANET) return false;
    int planetIndex = warpTarget.planetIndex;
    if (warpTarget.bodyId != 0u) {
        planetIndex = -1;
        for (int i = 0; i < system.planetCount; i++) {
            if (system.planets[i].bodyId == warpTarget.bodyId) {
                planetIndex = i;
                break;
            }
        }
    }
    if (planetIndex < 0 || planetIndex >= system.planetCount) return false;

    if (center) *center = SolarSystemPlanetCenter(&system, planetIndex);
    if (safeDistance) {
        *safeDistance = SolarSystemPlanetParkingRadiusGame(
            &system, planetIndex);
    }
    return true;
}

static bool LockWarpTarget(const Player *player, Vector3 forward)
{
    if (WorldIsSurfaceActive()) {
        SetImportMessage("Launch into space before locking a warp target.");
        return false;
    }

    SpaceBodyInfo body;
    if (!SpacePlanetNavigationPick(player->position, forward, &body)) {
        SetImportMessage("Move a planet marker near the crosshair to lock it.");
        return false;
    }

    warpTarget.locked = true;
    warpTarget.type = WARP_TARGET_PLANET;
    warpTarget.systemAnchorX = body.systemAnchorX;
    warpTarget.systemAnchorZ = body.systemAnchorZ;
    warpTarget.bodyId = body.bodyId;
    warpTarget.planetIndex = body.index - 1;
    snprintf(warpTarget.name, sizeof(warpTarget.name), "%s", body.name);
    warping = false;
    SetImportMessage(TextFormat("Locked %s. Press G to engage warp.", warpTarget.name));
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

    float gap = Vector3Distance(player->position, system.center) -
                SolarSystemParkingRadiusGame(&system);
    if (gap <= 1.0f) {
        player->velocity = Vector3Zero();
        SetImportMessage("Already within this star system's approach zone.");
        return false;
    }

    warpTarget.locked = true;
    warpTarget.type = WARP_TARGET_SYSTEM;
    warpTarget.systemAnchorX = systemAnchorX;
    warpTarget.systemAnchorZ = systemAnchorZ;
    warpTarget.bodyId = 0u;
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
    if (fuel <= 0.0f) {
        SetImportMessage("Warp unavailable: ship is out of fuel.");
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
    if (gap <= WarpArrivalTolerance(safeDistance)) {
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
    if (warping || !driving || fuel <= 0.0f) return;
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

#define SHIP_VISUAL_MAX_FACES 128

static Model shipModel = { 0 };

static Color ShipVisualShade(Color color, Vector3 normal)
{
    float shade = 0.80f;
    if (normal.y > 0.5f) shade = 1.06f;
    else if (normal.y < -0.5f) shade = 0.58f;
    else if (normal.z > 0.5f) shade = 0.94f;
    else if (normal.z < -0.5f) shade = 0.70f;
    else shade = normal.x > 0.0f ? 0.86f : 0.76f;
    return (Color){
        (unsigned char)Clamp((float)color.r * shade, 0.0f, 255.0f),
        (unsigned char)Clamp((float)color.g * shade, 0.0f, 255.0f),
        (unsigned char)Clamp((float)color.b * shade, 0.0f, 255.0f),
        color.a
    };
}

static bool ShipVisualAddQuad(Mesh *mesh, int *vertexIndex,
                              Vector3 a, Vector3 b, Vector3 c, Vector3 d,
                              BlockType textureBlock, Color color)
{
    if (!mesh || !vertexIndex || *vertexIndex < 0 ||
        *vertexIndex + 6 > mesh->vertexCount) return false;

    Vector3 normal = Vector3Normalize(Vector3CrossProduct(
        Vector3Subtract(b, a), Vector3Subtract(c, a)));
    if (Vector3LengthSqr(normal) < 0.5f) return false;

    Vector3 corners[6] = { a, b, c, a, c, d };
    Vector2 uvs[6] = { 0 };
    AtlasUVs(TextureForBlockFace(textureBlock, 2), uvs);
    Color shaded = ShipVisualShade(color, normal);
    for (int i = 0; i < 6; i++) {
        int index = (*vertexIndex)++;
        mesh->vertices[index * 3 + 0] = corners[i].x;
        mesh->vertices[index * 3 + 1] = corners[i].y;
        mesh->vertices[index * 3 + 2] = corners[i].z;
        mesh->texcoords[index * 2 + 0] = uvs[i].x;
        mesh->texcoords[index * 2 + 1] = uvs[i].y;
        mesh->normals[index * 3 + 0] = normal.x;
        mesh->normals[index * 3 + 1] = normal.y;
        mesh->normals[index * 3 + 2] = normal.z;
        mesh->colors[index * 4 + 0] = shaded.r;
        mesh->colors[index * 4 + 1] = shaded.g;
        mesh->colors[index * 4 + 2] = shaded.b;
        mesh->colors[index * 4 + 3] = shaded.a;
    }
    return true;
}

static bool ShipVisualAddBox(Mesh *mesh, int *vertexIndex,
                             Vector3 min, Vector3 max,
                             BlockType textureBlock, Color color)
{
    return ShipVisualAddQuad(
               mesh, vertexIndex,
               (Vector3){ max.x, min.y, max.z }, (Vector3){ max.x, min.y, min.z },
               (Vector3){ max.x, max.y, min.z }, (Vector3){ max.x, max.y, max.z },
               textureBlock, color) &&
           ShipVisualAddQuad(
               mesh, vertexIndex,
               (Vector3){ min.x, min.y, min.z }, (Vector3){ min.x, min.y, max.z },
               (Vector3){ min.x, max.y, max.z }, (Vector3){ min.x, max.y, min.z },
               textureBlock, color) &&
           ShipVisualAddQuad(
               mesh, vertexIndex,
               (Vector3){ min.x, max.y, max.z }, (Vector3){ max.x, max.y, max.z },
               (Vector3){ max.x, max.y, min.z }, (Vector3){ min.x, max.y, min.z },
               textureBlock, color) &&
           ShipVisualAddQuad(
               mesh, vertexIndex,
               (Vector3){ min.x, min.y, min.z }, (Vector3){ max.x, min.y, min.z },
               (Vector3){ max.x, min.y, max.z }, (Vector3){ min.x, min.y, max.z },
               textureBlock, color) &&
           ShipVisualAddQuad(
               mesh, vertexIndex,
               (Vector3){ min.x, min.y, max.z }, (Vector3){ max.x, min.y, max.z },
               (Vector3){ max.x, max.y, max.z }, (Vector3){ min.x, max.y, max.z },
               textureBlock, color) &&
           ShipVisualAddQuad(
               mesh, vertexIndex,
               (Vector3){ max.x, min.y, min.z }, (Vector3){ min.x, min.y, min.z },
               (Vector3){ min.x, max.y, min.z }, (Vector3){ max.x, max.y, min.z },
               textureBlock, color);
}

static bool ShipVisualAddTaperedSection(
    Mesh *mesh, int *vertexIndex,
    float rearZ, float rearY, float rearHalfWidth, float rearHalfHeight,
    float frontZ, float frontY, float frontHalfWidth, float frontHalfHeight,
    BlockType textureBlock, Color color)
{
    Vector3 rear[4] = {
        { -rearHalfWidth, rearY - rearHalfHeight, rearZ },
        { rearHalfWidth, rearY - rearHalfHeight, rearZ },
        { rearHalfWidth, rearY + rearHalfHeight, rearZ },
        { -rearHalfWidth, rearY + rearHalfHeight, rearZ }
    };
    Vector3 front[4] = {
        { -frontHalfWidth, frontY - frontHalfHeight, frontZ },
        { frontHalfWidth, frontY - frontHalfHeight, frontZ },
        { frontHalfWidth, frontY + frontHalfHeight, frontZ },
        { -frontHalfWidth, frontY + frontHalfHeight, frontZ }
    };
    return ShipVisualAddQuad(mesh, vertexIndex, front[0], front[1], front[2], front[3],
                             textureBlock, color) &&
           ShipVisualAddQuad(mesh, vertexIndex, rear[1], rear[0], rear[3], rear[2],
                             textureBlock, color) &&
           ShipVisualAddQuad(mesh, vertexIndex, front[1], rear[1], rear[2], front[2],
                             textureBlock, color) &&
           ShipVisualAddQuad(mesh, vertexIndex, rear[0], front[0], front[3], rear[3],
                             textureBlock, color) &&
           ShipVisualAddQuad(mesh, vertexIndex, front[3], front[2], rear[2], rear[3],
                             textureBlock, color) &&
           ShipVisualAddQuad(mesh, vertexIndex, rear[0], rear[1], front[1], front[0],
                             textureBlock, color);
}

// Points must wind clockwise when viewed from above so the top faces upward.
static bool ShipVisualAddWing(Mesh *mesh, int *vertexIndex,
                              const Vector2 points[4], float bottomY, float topY,
                              BlockType textureBlock, Color color)
{
    Vector3 bottom[4];
    Vector3 top[4];
    for (int i = 0; i < 4; i++) {
        bottom[i] = (Vector3){ points[i].x, bottomY, points[i].y };
        top[i] = (Vector3){ points[i].x, topY, points[i].y };
    }
    if (!ShipVisualAddQuad(mesh, vertexIndex, top[0], top[1], top[2], top[3],
                           textureBlock, color) ||
        !ShipVisualAddQuad(mesh, vertexIndex, bottom[3], bottom[2], bottom[1], bottom[0],
                           textureBlock, color)) return false;
    for (int i = 0; i < 4; i++) {
        int next = (i + 1) % 4;
        if (!ShipVisualAddQuad(mesh, vertexIndex,
                               bottom[i], bottom[next], top[next], top[i],
                               textureBlock, color)) return false;
    }
    return true;
}

static bool ShipVisualAllocateMesh(Mesh *mesh)
{
    if (!mesh) return false;
    mesh->vertexCount = SHIP_VISUAL_MAX_FACES * 6;
    mesh->triangleCount = SHIP_VISUAL_MAX_FACES * 2;
    mesh->vertices = malloc((size_t)mesh->vertexCount * 3 * sizeof(float));
    mesh->texcoords = malloc((size_t)mesh->vertexCount * 2 * sizeof(float));
    mesh->normals = malloc((size_t)mesh->vertexCount * 3 * sizeof(float));
    mesh->colors = malloc((size_t)mesh->vertexCount * 4 * sizeof(unsigned char));
    if (mesh->vertices && mesh->texcoords && mesh->normals && mesh->colors) return true;
    free(mesh->vertices);
    free(mesh->texcoords);
    free(mesh->normals);
    free(mesh->colors);
    *mesh = (Mesh){ 0 };
    return false;
}

void ShipLoadModel(void)
{
    if (shipModel.meshCount > 0) return;

    Mesh mesh = { 0 };
    if (!ShipVisualAllocateMesh(&mesh)) return;

    int vertexIndex = 0;
    const Vector2 leftWing[4] = {
        { -0.38f, -1.08f }, { -1.72f, -0.76f },
        { -1.88f, -0.12f }, { -0.38f, 0.66f }
    };
    const Vector2 rightWing[4] = {
        { 0.38f, 0.66f }, { 1.88f, -0.12f },
        { 1.72f, -0.76f }, { 0.38f, -1.08f }
    };
    const Color hull = { 226, 232, 238, 255 };
    const Color hullDark = { 132, 145, 158, 255 };
    const Color canopy = { 132, 194, 232, 255 };

    bool ok =
        ShipVisualAddTaperedSection(&mesh, &vertexIndex,
                                    -1.34f, 0.0f, 0.54f, 0.32f,
                                    0.76f, 0.02f, 0.46f, 0.29f,
                                    BLOCK_SPACESHIP, hull) &&
        ShipVisualAddTaperedSection(&mesh, &vertexIndex,
                                    0.76f, 0.02f, 0.46f, 0.29f,
                                    2.18f, -0.04f, 0.07f, 0.07f,
                                    BLOCK_WHITE, hull) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ -0.31f, -0.43f, -1.20f },
                         (Vector3){ 0.31f, -0.26f, 0.92f },
                         BLOCK_GRAY, hullDark) &&
        ShipVisualAddTaperedSection(&mesh, &vertexIndex,
                                    -0.28f, 0.43f, 0.34f, 0.18f,
                                    0.78f, 0.34f, 0.17f, 0.07f,
                                    BLOCK_GLASS, canopy) &&
        ShipVisualAddWing(&mesh, &vertexIndex, leftWing, -0.13f, 0.04f,
                          BLOCK_SPACESHIP, hull) &&
        ShipVisualAddWing(&mesh, &vertexIndex, rightWing, -0.13f, 0.04f,
                          BLOCK_SPACESHIP, hull) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ -0.83f, -0.25f, -1.58f },
                         (Vector3){ -0.47f, 0.19f, -0.55f },
                         BLOCK_BLACK, (Color){ 155, 166, 178, 255 }) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ 0.47f, -0.25f, -1.58f },
                         (Vector3){ 0.83f, 0.19f, -0.55f },
                         BLOCK_BLACK, (Color){ 155, 166, 178, 255 }) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ -0.80f, -0.21f, -1.72f },
                         (Vector3){ -0.50f, 0.15f, -1.57f },
                         BLOCK_GLOWSTONE, (Color){ 255, 198, 112, 255 }) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ 0.50f, -0.21f, -1.72f },
                         (Vector3){ 0.80f, 0.15f, -1.57f },
                         BLOCK_GLOWSTONE, (Color){ 255, 198, 112, 255 }) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ -0.67f, 0.12f, -1.42f },
                         (Vector3){ -0.49f, 0.67f, -0.92f },
                         BLOCK_SPACESHIP, hull) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ 0.49f, 0.12f, -1.42f },
                         (Vector3){ 0.67f, 0.67f, -0.92f },
                         BLOCK_SPACESHIP, hull) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ -1.89f, 0.00f, -0.49f },
                         (Vector3){ -1.72f, 0.17f, -0.22f },
                         BLOCK_RED, WHITE) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ 1.72f, 0.00f, -0.49f },
                         (Vector3){ 1.89f, 0.17f, -0.22f },
                         BLOCK_GREEN, WHITE) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ -0.55f, -0.04f, -0.32f },
                         (Vector3){ -0.48f, 0.10f, 0.67f },
                         BLOCK_ORANGE, WHITE) &&
        ShipVisualAddBox(&mesh, &vertexIndex,
                         (Vector3){ 0.48f, -0.04f, -0.32f },
                         (Vector3){ 0.55f, 0.10f, 0.67f },
                         BLOCK_ORANGE, WHITE);

    if (!ok || vertexIndex <= 0) {
        free(mesh.vertices);
        free(mesh.texcoords);
        free(mesh.normals);
        free(mesh.colors);
        return;
    }

    mesh.vertexCount = vertexIndex;
    mesh.triangleCount = vertexIndex / 3;
    UploadMesh(&mesh, false);
    shipModel = LoadModelFromMesh(mesh);
    if (shipModel.materialCount > 0) {
        SetMaterialTexture(&shipModel.materials[0], MATERIAL_MAP_DIFFUSE, blockAtlas);
    }
}

void ShipCleanup(void)
{
    if (shipModel.meshCount > 0) UnloadModel(shipModel);
    shipModel = (Model){ 0 };
}

void ShipDraw(const Player *player)
{
    if (!player || shipModel.meshCount == 0) return;
    shipModel.transform = MatrixRotateXYZ((Vector3){ player->pitch, player->yaw, 0.0f });
    Vector3 pos = Vector3Add(player->position, (Vector3){ 0.0f, 0.30f, 0.0f });
    DrawModel(shipModel, pos, 1.0f, WHITE);
}

void ShipUpdate(Player *player, float dt)
{
    if (!player) return;
    if (!isfinite(dt) || dt <= 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

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
    if (IsKeyPressed(KEY_G)) {
        if (!warpTarget.locked && !WorldIsSurfaceActive()) {
            if (LockWarpTarget(player, forward)) ToggleWarp(player);
        } else {
            ToggleWarp(player);
        }
    }

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

    if (!warping && translationInput) {
        float fuelRate = cruising ? SHIP_CRUISE_FUEL_RATE :
                                    SHIP_THRUST_FUEL_RATE;
        if (!ShipConsumeFuel(fuelRate * dt)) {
            cruising = false;
            translationInput = false;
            accel = Vector3Zero();
            SetImportMessage("Propulsion disabled: ship is out of fuel.");
        }
    }

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
            float arrivalTolerance = WarpArrivalTolerance(safeDistance);
            if (gap <= arrivalTolerance || targetDistance < 0.001f) {
                warping = false;
                player->velocity = Vector3Zero();
                if (warpTarget.type == WARP_TARGET_SYSTEM) {
                    char systemName[sizeof(warpTarget.name)];
                    snprintf(systemName, sizeof(systemName), "%s",
                             warpTarget.name);
                    ClearWarpTarget();
                    SetImportMessage(TextFormat(
                        "Arrived at %s. Aim near a planet marker and press G.",
                        systemName));
                } else {
                    SetImportMessage(TextFormat("Reached %s approach. Press E to land.",
                                                warpTarget.name));
                }
            } else {
                if (!ShipConsumeFuel(SHIP_WARP_FUEL_RATE * dt)) {
                    ClearWarpTarget();
                    player->velocity = Vector3Zero();
                    SetImportMessage("Warp halted: ship is out of fuel.");
                } else {
                    Vector3 warpDirection = Vector3Scale(toTarget, 1.0f / targetDistance);
                    player->yaw = atan2f(warpDirection.x, warpDirection.z);
                    player->pitch = asinf(Clamp(warpDirection.y, -1.0f, 1.0f));
                    forward = warpDirection;

                    float speed = Vector3Length(player->velocity);
                    float maxSafeSpeed = gap / fmaxf(dt, 0.001f);
                    float brakingSpeed = sqrtf(fmaxf(
                        0.0f, 2.0f * SHIP_WARP_DECEL *
                                  fmaxf(gap - arrivalTolerance, 0.0f)));
                    float desiredSpeed = fminf(SHIP_WARP_MAX_SPEED,
                                               fminf(brakingSpeed, maxSafeSpeed));
                    float rate = desiredSpeed > speed ? SHIP_WARP_ACCEL : SHIP_WARP_DECEL;
                    if (speed < desiredSpeed) speed = fminf(desiredSpeed, speed + rate * dt);
                    else speed = fmaxf(desiredSpeed, speed - rate * dt);
                    player->velocity = Vector3Scale(warpDirection, speed);
                }
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
            correctedSurfaceDist < SHIP_SPACE_COLLISION_CLEARANCE) {
            player->position = Vector3Add(
                player->position,
                Vector3Scale(Vector3Negate(correctionDir),
                             SHIP_SPACE_COLLISION_CLEARANCE -
                                 correctedSurfaceDist));
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
    cruising = false;
    player->floating = WorldIsSpaceActive();
    player->onGround = false;
    gravityPrimary = (SpaceGravitySample){ 0 };
    ClearWarpTarget();
    return true;
}

bool ShipExit(Player *player)
{
    if (!driving) return true;
    if (HomeWorldTryEnter(player)) {
        return ShipPlaceAfterExit(player);
    }
    if (PlanetWorldTryEnter(player)) {
        return ShipPlaceAfterExit(player);
    }
    return ShipPlaceAfterExit(player);
}

bool ShipForceExit(Player *player)
{
    if (!driving) return true;
    return ShipPlaceAfterExit(player);
}
