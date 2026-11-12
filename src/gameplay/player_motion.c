#include "gameplay/player_motion.h"

#include "gameplay/player_collision_internal.h"
#include "gameplay/ship.h"
#include "space/space_chunks.h"
#include "world/block_catalog.h"
#include "world/block_edits.h"
#include "world/block_types.h"
#include "world/chunks.h"
#include "world/surface_contract.h"
#include "world/world_environment.h"
#include "world/world_location.h"
#include "world/world_surface_snapshot.h"

#include "raymath.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PLAYER_COLLISION_STEP 0.25f
#define PLAYER_MAX_MOVE_SUBSTEPS 64u
#define PLAYER_CUBE_COLLISION_SKIN 0.001f

typedef struct PlayerCollisionBounds {
    int minX;
    int maxX;
    int minY;
    int maxY;
    int minZ;
    int maxZ;
} PlayerCollisionBounds;

static bool PlayerCubeBodyQuery(Vector3 position, WorldBlockBoxQuery *outBody);

bool IsSolidBlockAt(int x, int y, int z)
{
    BlockType type = GetBlockAt(x, y, z);
    return type != BLOCK_AIR && type != BLOCK_WATER;
}

static bool IsStairsBlock(BlockType type)
{
    return type == BLOCK_STONE_STAIRS || type == BLOCK_WOOD_STAIRS;
}

static float StairsCollisionTop(int x, int y, int z, float playerZ)
{
    (void)x;
    float zFrac = playerZ - (float)z;
    int step = (int)floorf(zFrac * 3.0f);
    if (step < 0) step = 0;
    if (step > 2) step = 2;
    return (float)y + (float)(step + 1) / 3.0f;
}

static bool PlayerCollisionBoundsAt(Vector3 position,
                                    PlayerCollisionBounds *outBounds)
{
    if (!outBounds || !isfinite(position.x) || !isfinite(position.y) ||
        !isfinite(position.z)) {
        return false;
    }

    double minX = floor((double)position.x - (double)PLAYER_RADIUS);
    double maxX = floor((double)position.x + (double)PLAYER_RADIUS);
    double minY = floor((double)position.y);
    double maxY = floor((double)position.y + (double)PLAYER_HEIGHT);
    double minZ = floor((double)position.z - (double)PLAYER_RADIUS);
    double maxZ = floor((double)position.z + (double)PLAYER_RADIUS);
    if (minX < (double)INT_MIN || maxX > (double)INT_MAX ||
        minY < (double)INT_MIN || maxY > (double)INT_MAX ||
        minZ < (double)INT_MIN || maxZ > (double)INT_MAX) {
        return false;
    }

    *outBounds = (PlayerCollisionBounds){
        .minX = (int)minX,
        .maxX = (int)maxX,
        .minY = (int)minY,
        .maxY = (int)maxY,
        .minZ = (int)minZ,
        .maxZ = (int)maxZ
    };
    return true;
}

static int PlayerChunkCoordinate(int blockCoordinate)
{
    if (blockCoordinate >= 0) return blockCoordinate / CHUNK_SIZE;
    return -(((-blockCoordinate - 1) / CHUNK_SIZE) + 1);
}

int PlayerMissingSurfaceChunkCount(Vector3 position)
{
    if (WorldIsSurfaceActive() &&
        SurfaceContractSupportsPhysicalPose(WorldCurrentSurfaceContract())) {
        WorldBlockBoxQuery body;
        if (!PlayerCubeBodyQuery(position, &body)) return 1;
        const SurfaceContract *contract = WorldCurrentSurfaceContract();
        SurfaceChunkHandle missing[WORLD_BLOCK_BOX_MAX_CELLS];
        unsigned missingCount = 0u;
        for (unsigned i = 0; i < body.count; i++) {
            WorldCell cell;
            if (SurfaceContractCellRead(
                    contract, body.cells[i].at.surface, &cell)) continue;
            SurfaceChunkHandle chunk;
            if (!SurfaceContractChunkFromAddress(
                    contract, body.cells[i].at.surface, &chunk)) {
                missingCount++;
                continue;
            }
            bool seen = false;
            for (unsigned j = 0; j < missingCount; j++) {
                if (SurfaceContractChunkEqual(contract, missing[j], chunk)) {
                    seen = true;
                    break;
                }
            }
            if (!seen && missingCount < WORLD_BLOCK_BOX_MAX_CELLS)
                missing[missingCount++] = chunk;
        }
        return (int)missingCount;
    }

    PlayerCollisionBounds bounds;
    if (!PlayerCollisionBoundsAt(position, &bounds)) return 0;
    int surfaceY = INT_MIN;
    for (int y = bounds.minY; y <= bounds.maxY; y++) {
        if (WorldBlockRegionAt(y) == WORLD_BLOCK_REGION_SURFACE) {
            surfaceY = y;
            break;
        }
    }
    if (surfaceY == INT_MIN) return 0;

    int missingCx[4] = { 0 };
    int missingCz[4] = { 0 };
    int missingCount = 0;
    for (int x = bounds.minX; x <= bounds.maxX; x++) {
        for (int z = bounds.minZ; z <= bounds.maxZ; z++) {
            if (SurfaceBlockReadyAt(x, surfaceY, z)) continue;
            int cx = PlayerChunkCoordinate(x);
            int cz = PlayerChunkCoordinate(z);
            bool found = false;
            for (int index = 0; index < missingCount; index++) {
                if (missingCx[index] == cx && missingCz[index] == cz) {
                    found = true;
                    break;
                }
            }
            if (!found && missingCount < 4) {
                missingCx[missingCount] = cx;
                missingCz[missingCount] = cz;
                missingCount++;
            }
        }
    }
    return missingCount;
}

static float PlayerGroundCeiling(Vector3 position)
{
    PlayerCollisionBounds bounds;
    if (!PlayerCollisionBoundsAt(position, &bounds)) return INFINITY;

    float minTop = INFINITY;
    for (int64_t x = bounds.minX; x <= bounds.maxX; x++) {
        for (int64_t y = bounds.minY; y <= bounds.maxY; y++) {
            for (int64_t z = bounds.minZ; z <= bounds.maxZ; z++) {
                BlockType type = GetBlockAt((int)x, (int)y, (int)z);
                float top;
                if (IsStairsBlock(type)) {
                    top = StairsCollisionTop((int)x, (int)y, (int)z,
                                             position.z);
                } else {
                    float height = BlockCollisionHeightAt((int)x, (int)y,
                                                          (int)z);
                    if (height <= 0.0f) continue;
                    top = (float)y + height;
                }
                if (top > position.y && top < minTop) minTop = top;
            }
        }
    }
    return minTop;
}

static bool TryStepUp(Player *player, Vector3 next, bool allowUnready)
{
    float ceiling = PlayerGroundCeiling(next);
    if (ceiling == INFINITY) return false;

    float rise = ceiling - player->position.y;
    if (rise <= 0.0f || rise > 0.5f) return false;

    Vector3 raised = next;
    raised.y += rise;
    if (PlayerOverlapsWorldWithOptions(raised, allowUnready)) return false;

    player->position.y += rise;
    player->position.x = next.x;
    player->position.z = next.z;
    return true;
}

/* Cube-world overlap test. On a cube surface the body is a physical box around
 * a body-centred pose, so it cannot share the legacy map-cell bounds. The
 * player's feet are `position`; the box is centred half a body above that. */
static bool PlayerCubeSolid(const WorldBlockBoxQuery *box, void *user)
{
    (void)user;
    const SurfaceContract *contract = WorldCurrentSurfaceContract();
    for (unsigned i = 0; i < box->count; i++) {
        WorldCell cell;
        if (!SurfaceContractCellRead(contract, box->cells[i].at.surface,
                                     &cell)) continue;
        if (cell.block >= BLOCK_COLOR_START && cell.block <= BLOCK_COLOR_END)
            return true;
        /* Decorative cross plants, carpets and open gates have zero collision
         * height even though they occupy a block cell. Cube collision must
         * follow that catalog shape just like the legacy path. */
        if (BlockCatalogGet(cell.block)->collisionHeight > 0.0f) return true;
    }
    return false;
}

static bool PlayerCubeKnown(const WorldBlockBoxQuery *box, void *user)
{
    (void)user;
    const SurfaceContract *contract = WorldCurrentSurfaceContract();
    for (unsigned i = 0; i < box->count; i++) {
        WorldCell cell;
        if (!SurfaceContractCellRead(contract, box->cells[i].at.surface,
                                     &cell)) return false;
    }
    return true;
}

static bool PlayerOverlapsCubeWorld(Vector3 position, bool allowUnready)
{
    WorldBlockBoxQuery body;
    if (!PlayerCubeBodyQuery(position, &body)) {
        /* An unenumerable body box means the cells could not be resolved. Keep
         * the legacy rule: blocked on foot, passable while driving a ship. */
        return !allowUnready;
    }
    /* Unknown terrain is a temporary wall, exactly as the legacy path treats a
     * streamed frontier; a ship may keep moving while chunks catch up. */
    if (!PlayerCubeKnown(&body, NULL)) return !allowUnready;
    return PlayerCubeSolid(&body, NULL);
}

static bool PlayerCubeBodyQuery(Vector3 position, WorldBlockBoxQuery *outBody)
{
    if (!outBody) return false;
    if (position.y < (float)SURFACE_MIN_Y ||
        position.y + PLAYER_HEIGHT > (float)SURFACE_MAX_Y_EXCLUSIVE)
        return false;
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    if (!context.active) return false;
    SurfaceMapProjection projection = SurfaceProjectMapCoordinates(
        (float)context.originX + position.x,
        (float)context.originZ + position.z);
    SurfacePose centre;
    if (!SurfacePoseFromGeographic(context.bodyId,
            (double)projection.longitude, (double)projection.latitude,
            (double)position.y + (double)PLAYER_HEIGHT * 0.5, 0.0, 0.0,
            &centre)) return false;
    const Vector3 half = {
        PLAYER_RADIUS - PLAYER_CUBE_COLLISION_SKIN,
        PLAYER_HEIGHT * 0.5f - PLAYER_CUBE_COLLISION_SKIN,
        PLAYER_RADIUS - PLAYER_CUBE_COLLISION_SKIN
    };
    return WorldBlockBoxCollect(&centre, half, outBody);
}

bool PlayerOverlapsWorldWithOptions(Vector3 position, bool allowUnready)
{
    /* On a cube surface the body is not a map-cell box; route to the cube test
     * so collision, picking and rendering agree on the same cells. */
    if (WorldIsSurfaceActive() &&
        SurfaceContractSupportsPhysicalPose(WorldCurrentSurfaceContract())) {
        return PlayerOverlapsCubeWorld(position, allowUnready);
    }

    PlayerCollisionBounds bounds;
    if (!PlayerCollisionBoundsAt(position, &bounds)) return true;

    if (WorldBlockRegionAt(NETHER_LAYER_Y) == WORLD_BLOCK_REGION_NETHER) {
        if (position.y < (float)NETHER_LAYER_Y) return true;
    } else if (WorldIsSurfaceActive() &&
               (position.y < (float)SURFACE_MIN_Y ||
                position.y + PLAYER_HEIGHT >
                    (float)SURFACE_MAX_Y_EXCLUSIVE)) {
        return true;
    }

    for (int64_t x = bounds.minX; x <= bounds.maxX; x++) {
        for (int64_t y = bounds.minY; y <= bounds.maxY; y++) {
            for (int64_t z = bounds.minZ; z <= bounds.maxZ; z++) {
                WorldBlockRegion region = WorldBlockRegionAt((int)y);
                bool ready = region == WORLD_BLOCK_REGION_SURFACE
                    ? SurfaceBlockReadyAt((int)x, (int)y, (int)z)
                    : (region != WORLD_BLOCK_REGION_SPACE ||
                       SpaceBlockReadyAt((int)x, (int)y, (int)z));
                if (!ready) {
                    // Streamed frontiers are temporary walls for a walking
                    // player. Ships may continue while chunks catch up.
                    if (allowUnready) continue;
                    return true;
                }
                BlockType type = GetBlockAt((int)x, (int)y, (int)z);
                float top;
                if (IsStairsBlock(type)) {
                    top = StairsCollisionTop((int)x, (int)y, (int)z,
                                             position.z);
                } else {
                    float height = BlockCollisionHeightAt((int)x, (int)y,
                                                          (int)z);
                    if (height <= 0.0f) continue;
                    top = (float)y + height;
                }
                if (position.y < top && position.y + PLAYER_HEIGHT > (float)y) return true;
            }
        }
    }
    return false;
}

bool PlayerOverlapsWorld(Vector3 position)
{
    return PlayerOverlapsWorldWithOptions(position, ShipIsDriving());
}

bool PlayerFindLandingSpot(Vector3 start, int minY, int maxY, Vector3 *out)
{
    Vector3 spot = start;
    if (spot.y < (float)minY) spot.y = (float)minY;
    if (spot.y > (float)maxY) spot.y = (float)maxY;

    int safety = 0;
    while (PlayerOverlapsWorld(spot) && safety < 12) {
        spot.y += 1.0f;
        safety++;
        if (spot.y > (float)maxY) spot.y = (float)maxY;
    }
    if (PlayerOverlapsWorld(spot)) return false;

    while (spot.y > (float)minY) {
        Vector3 below = spot;
        below.y -= 1.0f;
        if (below.y < (float)minY || PlayerOverlapsWorld(below)) break;
        spot = below;
    }
    if (out) *out = spot;
    return true;
}

static bool PlayerVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

/* Local <-> pose conversion. The legacy local position uses absolute world Y,
 * while a cube pose stores a radial offset above the mean radius, so the two
 * differ by a constant. Both horizontal axes are map cells. */
bool PlayerSurfacePoseFromLocal(Vector3 position, SurfacePose *outPose)
{
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    if (!context.active || !outPose) return false;
    float mapX = (float)context.originX + position.x;
    float mapZ = (float)context.originZ + position.z;
    SurfaceMapProjection projection = SurfaceProjectMapCoordinates(mapX, mapZ);
    /* Legacy Y and cube radial are both heights above the mean sphere; only
     * the cell-centre offset differs, and that is added by SurfaceCellCenter. */
    double radial = (double)position.y;
    return SurfacePoseFromGeographic(context.bodyId,
        (double)projection.longitude, (double)projection.latitude,
        radial, 0.0, 0.0, outPose);
}

bool PlayerLocalFromSurfacePose(const SurfacePose *pose, Vector3 *outPosition)
{
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    if (!context.active || !pose || !outPosition) return false;
    Vector2 map;
    if (!SurfaceMapCoordinatesFromDirection(
            (Vector3){ (float)pose->position.x, (float)pose->position.y,
                       (float)pose->position.z }, &map)) return false;
    outPosition->x = map.x - (float)context.originX;
    outPosition->z = map.y - (float)context.originZ;
    /* |position| is measured from the planet centre, so the legacy height is
     * that distance minus the mean sphere radius. */
    double length = sqrt((double)pose->position.x * (double)pose->position.x +
                         (double)pose->position.y * (double)pose->position.y +
                         (double)pose->position.z * (double)pose->position.z);
    outPosition->y = (float)(length - (double)SURFACE_RADIUS_BLOCKS);
    return true;
}

/* Physical displacement on a cube surface: the delta is in blocks along the
 * local right/forward/up axes, so it is applied as a great-circle move rather
 * than as a map-cell step. A map cell near a pole is a fraction of a block, so
 * stepping in cells would make movement latitude-dependent. */
/* Applies one displacement component and reports whether it was accepted.
 * Each axis is tried on its own so a blocked vertical move cannot veto the
 * horizontal one: with the whole delta tested at once, the per-frame gravity
 * term would push the endpoint into the floor and freeze walking entirely. */
static bool PlayerCubeTryAxis(Player *player, Vector3 delta, bool allowUnready)
{
    SurfacePose pose;
    if (!PlayerSurfacePoseFromLocal(player->position, &pose)) return false;
    SurfacePose next;
    if (!SurfacePoseDisplace(&pose, (double)delta.x, (double)delta.z,
                             (double)delta.y, &next)) return false;
    Vector3 candidate;
    if (!PlayerLocalFromSurfacePose(&next, &candidate)) return false;
    if (PlayerOverlapsWorldWithOptions(candidate, allowUnready)) return false;
    player->position = candidate;
    return true;
}

static bool PlayerCubeDisplace(Player *player, Vector3 delta, bool allowUnready)
{
    /* Long steps are split so a fast fall cannot tunnel through a floor. The
     * cube path has no per-axis stepper to fall back on, so the subdivision
     * lives here. */
    float span = fmaxf(fabsf(delta.x), fmaxf(fabsf(delta.y), fabsf(delta.z)));
    unsigned steps = 1u;
    if (span > PLAYER_COLLISION_STEP)
        steps = (unsigned)ceilf(span / PLAYER_COLLISION_STEP);
    if (steps > PLAYER_MAX_MOVE_SUBSTEPS) steps = PLAYER_MAX_MOVE_SUBSTEPS;
    Vector3 step = Vector3Scale(delta, 1.0f / (float)steps);

    bool moved = false;
    for (unsigned i = 0; i < steps; i++) {
        /* Vertical first: landing must be detected before the horizontal trial
         * uses the new height, and it is also the axis that sets onGround. */
        bool verticalOk = PlayerCubeTryAxis(
            player, (Vector3){0.0f, step.y, 0.0f}, allowUnready);
        if (!verticalOk) {
            if (step.y <= 0.0f) {
                player->onGround = true;
                player->velocity.y = 0.0f;
            } else {
                player->velocity.y = 0.0f;
            }
        } else if (step.y > 0.0f) {
            player->onGround = false;
            moved = true;
        }
        if (!PlayerCubeTryAxis(
                player, (Vector3){step.x, 0.0f, 0.0f}, allowUnready)) {
            player->velocity.x = 0.0f;
        } else if (step.x != 0.0f) {
            moved = true;
        }
        if (!PlayerCubeTryAxis(
                player, (Vector3){0.0f, 0.0f, step.z}, allowUnready)) {
            player->velocity.z = 0.0f;
        } else if (step.z != 0.0f) {
            moved = true;
        }
    }
    return moved;
}

static void MovePlayerStep(Player *player, Vector3 delta, bool canStepUp,
                           bool allowUnready)
{
    Vector3 next = player->position;
    next.x += delta.x;
    if (!PlayerOverlapsWorldWithOptions(next, allowUnready)) {
        player->position.x = next.x;
    } else {
        player->velocity.x = 0.0f;
        if (canStepUp) TryStepUp(player, next, allowUnready);
    }

    next = player->position;
    next.z += delta.z;
    if (!PlayerOverlapsWorldWithOptions(next, allowUnready)) {
        player->position.z = next.z;
    } else {
        player->velocity.z = 0.0f;
        if (canStepUp) TryStepUp(player, next, allowUnready);
    }

    next = player->position;
    next.y += delta.y;
    player->onGround = false;
    if (!PlayerOverlapsWorldWithOptions(next, allowUnready)) {
        player->position.y = next.y;
    } else {
        if (delta.y < 0.0f) player->onGround = true;
        player->velocity.y = 0.0f;
    }
}

void PlayerMoveWithOptions(Player *player, Vector3 delta, bool allowUnready,
                           bool canStepUp)
{
    if (!player || !PlayerVectorIsFinite(player->position) ||
        !PlayerVectorIsFinite(delta)) {
        return;
    }

    /* A cube surface moves by physical displacement; the per-axis cell stepper
     * below assumes map cells have stable width, which a cube grid does not. */
    if (WorldIsSurfaceActive() &&
        SurfaceContractSupportsPhysicalPose(WorldCurrentSurfaceContract())) {
        /* Clear the flag first: PlayerCubeDisplace sets it only on a blocked
         * downward move, so a player walking off a ledge correctly falls. */
        if (delta.y < 0.0f) player->onGround = false;
        PlayerCubeDisplace(player, delta, allowUnready);
        return;
    }

    canStepUp = canStepUp && WorldIsSurfaceActive();
    float maxDistance = fmaxf(fabsf(delta.x),
                              fmaxf(fabsf(delta.y), fabsf(delta.z)));
    float maxAllowedDistance = PLAYER_COLLISION_STEP *
                               (float)PLAYER_MAX_MOVE_SUBSTEPS;
    unsigned substeps;
    if (maxDistance > maxAllowedDistance) {
        substeps = PLAYER_MAX_MOVE_SUBSTEPS;
        float scale = maxAllowedDistance / maxDistance;
        delta = Vector3Scale(delta, scale);
    } else {
        substeps = (unsigned)ceilf(maxDistance / PLAYER_COLLISION_STEP);
        if (substeps == 0u) substeps = 1u;
    }

    Vector3 step = Vector3Scale(delta, 1.0f / (float)substeps);
    for (unsigned index = 0; index < substeps; index++) {
        MovePlayerStep(player, step, canStepUp, allowUnready);
    }
}

void MovePlayer(Player *player, Vector3 delta)
{
    bool canStepUp = player && player->onGround && !player->floating;
    PlayerMoveWithOptions(player, delta, ShipIsDriving(), canStepUp);
}

Vector3 ForwardFromAngles(float yaw, float pitch)
{
    return Vector3Normalize((Vector3){
        sinf(yaw) * cosf(pitch),
        sinf(pitch),
        cosf(yaw) * cosf(pitch)
    });
}

Vector3 FlatForward(float yaw)
{
    return Vector3Normalize((Vector3){ sinf(yaw), 0.0f, cosf(yaw) });
}

Vector3 RightFromYaw(float yaw)
{
    /* Match the camera's screen-right basis: cross(forward, world-up). The
     * opposite convention made D move left while the camera still looked in
     * the ForwardFromAngles() direction. */
    return Vector3Normalize((Vector3){ -cosf(yaw), 0.0f, sinf(yaw) });
}
