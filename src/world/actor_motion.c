#include "world/actor_motion.h"

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
#include <stddef.h>
#include <stdint.h>

#define ACTOR_COLLISION_STEP 0.25f
#define ACTOR_MAX_MOVE_SUBSTEPS 64u
#define ACTOR_CUBE_COLLISION_SKIN 0.001f

typedef struct ActorCollisionBounds {
    int minX;
    int maxX;
    int minY;
    int maxY;
    int minZ;
    int maxZ;
} ActorCollisionBounds;

static bool ActorCubeBodyQuery(const ActorCollider *collider, Vector3 position,
                               WorldBlockBoxQuery *outBody);

static bool ActorVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static bool ActorColliderIsValid(const ActorCollider *collider)
{
    return collider && isfinite(collider->radius) &&
        isfinite(collider->height) && isfinite(collider->stepHeight) &&
        collider->radius > 0.0f && collider->height > 0.0f &&
        collider->stepHeight >= 0.0f;
}

static bool IsStairsBlock(BlockType type)
{
    return type == BLOCK_STONE_STAIRS || type == BLOCK_WOOD_STAIRS;
}

static float StairsCollisionTop(int x, int y, int z, float actorZ)
{
    (void)x;
    float zFraction = actorZ - (float)z;
    int step = (int)floorf(zFraction * 3.0f);
    if (step < 0) step = 0;
    if (step > 2) step = 2;
    return (float)y + (float)(step + 1) / 3.0f;
}

static bool ActorCollisionBoundsAt(const ActorCollider *collider,
                                   Vector3 position,
                                   ActorCollisionBounds *outBounds)
{
    if (!ActorColliderIsValid(collider) || !outBounds ||
        !ActorVectorIsFinite(position)) {
        return false;
    }

    double minX = floor((double)position.x - (double)collider->radius);
    double maxX = floor((double)position.x + (double)collider->radius);
    double minY = floor((double)position.y);
    double maxY = floor((double)position.y + (double)collider->height);
    double minZ = floor((double)position.z - (double)collider->radius);
    double maxZ = floor((double)position.z + (double)collider->radius);
    if (minX < (double)INT_MIN || maxX > (double)INT_MAX ||
        minY < (double)INT_MIN || maxY > (double)INT_MAX ||
        minZ < (double)INT_MIN || maxZ > (double)INT_MAX) {
        return false;
    }

    *outBounds = (ActorCollisionBounds){
        .minX = (int)minX,
        .maxX = (int)maxX,
        .minY = (int)minY,
        .maxY = (int)maxY,
        .minZ = (int)minZ,
        .maxZ = (int)maxZ
    };
    return true;
}

static int ActorChunkCoordinate(int blockCoordinate)
{
    if (blockCoordinate >= 0) return blockCoordinate / CHUNK_SIZE;
    return -(((-blockCoordinate - 1) / CHUNK_SIZE) + 1);
}

int WorldActorMissingSurfaceChunkCount(const ActorCollider *collider,
                                       Vector3 position)
{
    if (WorldIsSurfaceActive() &&
        SurfaceContractSupportsPhysicalPose(WorldCurrentSurfaceContract())) {
        WorldBlockBoxQuery body;
        if (!ActorCubeBodyQuery(collider, position, &body)) return 1;
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

    ActorCollisionBounds bounds;
    if (!ActorCollisionBoundsAt(collider, position, &bounds)) return 0;
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
            int cx = ActorChunkCoordinate(x);
            int cz = ActorChunkCoordinate(z);
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

static float ActorGroundCeiling(const ActorCollider *collider, Vector3 position)
{
    ActorCollisionBounds bounds;
    if (!ActorCollisionBoundsAt(collider, position, &bounds)) return INFINITY;

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

static bool TryActorStepUp(const ActorCollider *collider, ActorBody *body,
                           Vector3 next, bool allowUnready)
{
    float ceiling = ActorGroundCeiling(collider, next);
    if (ceiling == INFINITY) return false;

    float rise = ceiling - body->position.y;
    if (rise <= 0.0f || rise > collider->stepHeight) return false;

    Vector3 raised = next;
    raised.y += rise;
    if (WorldActorOverlaps(collider, raised, allowUnready)) return false;

    body->position.y += rise;
    body->position.x = next.x;
    body->position.z = next.z;
    return true;
}

/* Cube-world overlap test. On a cube surface the body is a physical box around
 * a body-centred pose, so it cannot share the legacy map-cell bounds. The
 * actor's feet are `position`; the box is centred half a body above that. */
static bool ActorCubeSolid(const WorldBlockBoxQuery *box, void *user)
{
    (void)user;
    const SurfaceContract *contract = WorldCurrentSurfaceContract();
    for (unsigned i = 0; i < box->count; i++) {
        WorldCell cell;
        if (!SurfaceContractCellRead(contract, box->cells[i].at.surface,
                                     &cell)) continue;
        if (cell.block >= BLOCK_COLOR_START && cell.block <= BLOCK_COLOR_END)
            return true;
        if (BlockCatalogGet(cell.block)->collisionHeight > 0.0f) return true;
    }
    return false;
}

static bool ActorCubeKnown(const WorldBlockBoxQuery *box, void *user)
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

static bool ActorOverlapsCubeWorld(const ActorCollider *collider,
                                   Vector3 position, bool allowUnready)
{
    WorldBlockBoxQuery body;
    if (!ActorCubeBodyQuery(collider, position, &body))
        return !allowUnready;
    if (!ActorCubeKnown(&body, NULL)) return !allowUnready;
    return ActorCubeSolid(&body, NULL);
}

static bool ActorCubeBodyQuery(const ActorCollider *collider, Vector3 position,
                               WorldBlockBoxQuery *outBody)
{
    if (!outBody || !ActorColliderIsValid(collider)) return false;
    if (position.y < (float)SURFACE_MIN_Y ||
        position.y + collider->height > (float)SURFACE_MAX_Y_EXCLUSIVE)
        return false;
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    if (!context.active) return false;
    SurfaceMapProjection projection = SurfaceProjectMapCoordinates(
        (float)context.originX + position.x,
        (float)context.originZ + position.z);
    SurfacePose centre;
    if (!SurfacePoseFromGeographic(context.bodyId,
            (double)projection.longitude, (double)projection.latitude,
            (double)position.y + (double)collider->height * 0.5, 0.0, 0.0,
            &centre)) return false;
    const Vector3 half = {
        collider->radius - ACTOR_CUBE_COLLISION_SKIN,
        collider->height * 0.5f - ACTOR_CUBE_COLLISION_SKIN,
        collider->radius - ACTOR_CUBE_COLLISION_SKIN
    };
    return WorldBlockBoxCollect(&centre, half, outBody);
}

bool WorldActorOverlaps(const ActorCollider *collider, Vector3 position,
                        bool allowUnready)
{
    if (!ActorColliderIsValid(collider)) return true;
    if (WorldIsSurfaceActive() &&
        SurfaceContractSupportsPhysicalPose(WorldCurrentSurfaceContract())) {
        return ActorOverlapsCubeWorld(collider, position, allowUnready);
    }

    ActorCollisionBounds bounds;
    if (!ActorCollisionBoundsAt(collider, position, &bounds)) return true;

    if (WorldBlockRegionAt(NETHER_LAYER_Y) == WORLD_BLOCK_REGION_NETHER) {
        if (position.y < (float)NETHER_LAYER_Y) return true;
    } else if (WorldIsSurfaceActive() &&
               (position.y < (float)SURFACE_MIN_Y ||
                position.y + collider->height >
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
                if (position.y < top &&
                    position.y + collider->height > (float)y) return true;
            }
        }
    }
    return false;
}

bool WorldActorFindLandingSpot(const ActorCollider *collider, Vector3 start,
                               int minY, int maxY, bool allowUnready,
                               Vector3 *out)
{
    if (!ActorColliderIsValid(collider)) return false;
    Vector3 spot = start;
    if (spot.y < (float)minY) spot.y = (float)minY;
    if (spot.y > (float)maxY) spot.y = (float)maxY;

    int safety = 0;
    while (WorldActorOverlaps(collider, spot, allowUnready) && safety < 12) {
        spot.y += 1.0f;
        safety++;
        if (spot.y > (float)maxY) spot.y = (float)maxY;
    }
    if (WorldActorOverlaps(collider, spot, allowUnready)) return false;

    while (spot.y > (float)minY) {
        Vector3 below = spot;
        below.y -= 1.0f;
        if (below.y < (float)minY ||
            WorldActorOverlaps(collider, below, allowUnready)) break;
        spot = below;
    }
    if (out) *out = spot;
    return true;
}

/* Local <-> pose conversion. The legacy local position uses absolute world Y,
 * while a cube pose stores a radial offset above the mean radius, so the two
 * differ by a constant. Both horizontal axes are map cells. */
bool WorldActorPoseFromLocal(Vector3 position, SurfacePose *outPose)
{
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    if (!context.active || !outPose) return false;
    float mapX = (float)context.originX + position.x;
    float mapZ = (float)context.originZ + position.z;
    SurfaceMapProjection projection = SurfaceProjectMapCoordinates(mapX, mapZ);
    return SurfacePoseFromGeographic(context.bodyId,
        (double)projection.longitude, (double)projection.latitude,
        (double)position.y, 0.0, 0.0, outPose);
}

bool WorldActorLocalFromPose(const SurfacePose *pose, Vector3 *outPosition)
{
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    if (!context.active || !pose || !outPosition) return false;
    Vector2 map;
    if (!SurfaceMapCoordinatesFromDirection(
            (Vector3){ (float)pose->position.x, (float)pose->position.y,
                       (float)pose->position.z }, &map)) return false;
    outPosition->x = map.x - (float)context.originX;
    outPosition->z = map.y - (float)context.originZ;
    double length = sqrt((double)pose->position.x * (double)pose->position.x +
                         (double)pose->position.y * (double)pose->position.y +
                         (double)pose->position.z * (double)pose->position.z);
    outPosition->y = (float)(length - (double)SURFACE_RADIUS_BLOCKS);
    return true;
}

static bool ActorCubeTryAxis(const ActorCollider *collider, ActorBody *body,
                             Vector3 delta, bool allowUnready)
{
    SurfacePose pose;
    if (!WorldActorPoseFromLocal(body->position, &pose)) return false;
    SurfacePose next;
    if (!SurfacePoseDisplace(&pose, (double)delta.x, (double)delta.z,
                             (double)delta.y, &next)) return false;
    Vector3 candidate;
    if (!WorldActorLocalFromPose(&next, &candidate)) return false;
    if (WorldActorOverlaps(collider, candidate, allowUnready)) return false;
    body->position = candidate;
    return true;
}

static bool ActorCubeDisplace(const ActorCollider *collider, ActorBody *body,
                              Vector3 delta, bool allowUnready)
{
    float span = fmaxf(fabsf(delta.x), fmaxf(fabsf(delta.y), fabsf(delta.z)));
    unsigned steps = 1u;
    if (span > ACTOR_COLLISION_STEP)
        steps = (unsigned)ceilf(span / ACTOR_COLLISION_STEP);
    if (steps > ACTOR_MAX_MOVE_SUBSTEPS) steps = ACTOR_MAX_MOVE_SUBSTEPS;
    Vector3 step = Vector3Scale(delta, 1.0f / (float)steps);

    bool moved = false;
    for (unsigned i = 0; i < steps; i++) {
        bool verticalOk = ActorCubeTryAxis(
            collider, body, (Vector3){0.0f, step.y, 0.0f}, allowUnready);
        if (!verticalOk) {
            if (step.y <= 0.0f) {
                body->onGround = true;
                body->velocity.y = 0.0f;
            } else {
                body->velocity.y = 0.0f;
            }
        } else if (step.y > 0.0f) {
            body->onGround = false;
            moved = true;
        }
        if (!ActorCubeTryAxis(
                collider, body, (Vector3){step.x, 0.0f, 0.0f}, allowUnready)) {
            body->velocity.x = 0.0f;
        } else if (step.x != 0.0f) {
            moved = true;
        }
        if (!ActorCubeTryAxis(
                collider, body, (Vector3){0.0f, 0.0f, step.z}, allowUnready)) {
            body->velocity.z = 0.0f;
        } else if (step.z != 0.0f) {
            moved = true;
        }
    }
    return moved;
}

static void MoveActorStep(const ActorCollider *collider, ActorBody *body,
                          Vector3 delta, bool canStepUp, bool allowUnready)
{
    Vector3 next = body->position;
    next.x += delta.x;
    if (!WorldActorOverlaps(collider, next, allowUnready)) {
        body->position.x = next.x;
    } else {
        body->velocity.x = 0.0f;
        if (canStepUp) TryActorStepUp(collider, body, next, allowUnready);
    }

    next = body->position;
    next.z += delta.z;
    if (!WorldActorOverlaps(collider, next, allowUnready)) {
        body->position.z = next.z;
    } else {
        body->velocity.z = 0.0f;
        if (canStepUp) TryActorStepUp(collider, body, next, allowUnready);
    }

    next = body->position;
    next.y += delta.y;
    body->onGround = false;
    if (!WorldActorOverlaps(collider, next, allowUnready)) {
        body->position.y = next.y;
    } else {
        if (delta.y < 0.0f) body->onGround = true;
        body->velocity.y = 0.0f;
    }
}

bool WorldActorResolveMotion(const ActorBody *body,
                             const ActorCollider *collider,
                             const ActorMotionRequest *request,
                             ActorMotionResult *outResult)
{
    if (!outResult) return false;
    *outResult = (ActorMotionResult){ 0 };
    if (!body || !ActorColliderIsValid(collider) || !request ||
        !ActorVectorIsFinite(body->position) ||
        !ActorVectorIsFinite(request->delta)) {
        return false;
    }

    ActorBody working = *body;
    if (WorldIsSurfaceActive() &&
        SurfaceContractSupportsPhysicalPose(WorldCurrentSurfaceContract())) {
        if (request->delta.y < 0.0f) working.onGround = false;
        outResult->moved = ActorCubeDisplace(
            collider, &working, request->delta, request->allowUnready);
    } else {
        bool canStepUp = request->canStepUp && WorldIsSurfaceActive();
        float maxDistance = fmaxf(fabsf(request->delta.x),
            fmaxf(fabsf(request->delta.y), fabsf(request->delta.z)));
        float maxAllowedDistance = ACTOR_COLLISION_STEP *
            (float)ACTOR_MAX_MOVE_SUBSTEPS;
        unsigned substeps;
        Vector3 delta = request->delta;
        if (maxDistance > maxAllowedDistance) {
            substeps = ACTOR_MAX_MOVE_SUBSTEPS;
            float scale = maxAllowedDistance / maxDistance;
            delta = Vector3Scale(delta, scale);
        } else {
            substeps = (unsigned)ceilf(maxDistance / ACTOR_COLLISION_STEP);
            if (substeps == 0u) substeps = 1u;
        }

        Vector3 step = Vector3Scale(delta, 1.0f / (float)substeps);
        for (unsigned index = 0; index < substeps; index++) {
            MoveActorStep(collider, &working, step, canStepUp,
                          request->allowUnready);
        }
        outResult->moved = Vector3DistanceSqr(body->position,
                                               working.position) > 0.0f;
    }
    outResult->body = working;
    return true;
}
