#include "interaction.h"
#include "space.h"
#include "world.h"
#include "world_environment.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

static int solidX = -1;
static int blockQueries = 0;

BlockType GetBlockAt(int x, int y, int z)
{
    blockQueries++;
    return x == solidX && y == 0 && z == 0 ? BLOCK_STONE : BLOCK_AIR;
}

float BlockCollisionHeight(BlockType type)
{
    return type == BLOCK_AIR ? 0.0f : 1.0f;
}

bool IsTranslucentBlock(BlockType type)
{
    (void)type;
    return false;
}

WorldBlockRegion WorldBlockRegionAt(int y)
{
    (void)y;
    return WORLD_BLOCK_REGION_SURFACE;
}

bool SpaceBlockReadyAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
    return true;
}

static void ResetRayWorld(void)
{
    solidX = -1;
    blockQueries = 0;
}

static void TestRaycastRejectsInvalidInput(void)
{
    ResetRayWorld();
    Vector3 origin = { 0.1f, 0.1f, 0.1f };
    Vector3 zero = { 0.0f, 0.0f, 0.0f };
    Vector3 nanDirection = { NAN, 0.0f, 0.0f };
    Vector3 nanOrigin = { NAN, 0.0f, 0.0f };
    Vector3 hugeOrigin = { (float)INT_MAX, 0.0f, 0.0f };

    assert(!RaycastBlocks(origin, zero, 4.0f).hit);
    assert(RaycastCameraOcclusion(origin, zero, 4.0f) < 0.0f);
    assert(!RaycastBlocks(origin, nanDirection, 4.0f).hit);
    assert(RaycastCameraOcclusion(nanOrigin, (Vector3){ 1.0f, 0.0f, 0.0f },
                                  4.0f) < 0.0f);
    assert(!RaycastBlocks(origin, (Vector3){ 1.0f, 0.0f, 0.0f }, -1.0f).hit);
    assert(RaycastCameraOcclusion(origin, (Vector3){ 1.0f, 0.0f, 0.0f },
                                  INFINITY) < 0.0f);
    assert(!RaycastBlocks(hugeOrigin, (Vector3){ 1.0f, 0.0f, 0.0f },
                          4.0f).hit);
    assert(blockQueries == 0);
}

static void TestRaycastNormalizesDirection(void)
{
    ResetRayWorld();
    solidX = 1;
    Vector3 origin = { 0.1f, 0.1f, 0.1f };
    HitResult hit = RaycastBlocks(
        origin, (Vector3){ 2.0f, 0.0f, 0.0f }, 0.5f);
    assert(!hit.hit);
    assert(RaycastCameraOcclusion(
               origin, (Vector3){ 2.0f, 0.0f, 0.0f }, 0.5f) < 0.0f);

    hit = RaycastBlocks(origin, (Vector3){ 2.0f, 0.0f, 0.0f }, 2.0f);
    assert(hit.hit);
    assert(hit.x == 1 && hit.y == 0 && hit.z == 0);
    assert(hit.nx == -1 && hit.ny == 0 && hit.nz == 0);
    float occlusion = RaycastCameraOcclusion(
        origin, (Vector3){ 2.0f, 0.0f, 0.0f }, 2.0f);
    assert(fabsf(occlusion - 0.9f) < 0.0001f);
}

static void TestRaycastHasIterationLimit(void)
{
    ResetRayWorld();
    HitResult hit = RaycastBlocks(
        (Vector3){ 0.1f, 0.1f, 0.1f },
        (Vector3){ 1.0f, 0.0f, 0.0f }, FLT_MAX);
    assert(!hit.hit);
    assert(blockQueries <= 4096);

    blockQueries = 0;
    assert(RaycastCameraOcclusion(
               (Vector3){ 0.1f, 0.1f, 0.1f },
               (Vector3){ 1.0f, 0.0f, 0.0f }, FLT_MAX) < 0.0f);
    assert(blockQueries <= 4096);
}

int main(void)
{
    TestRaycastRejectsInvalidInput();
    TestRaycastNormalizesDirection();
    TestRaycastHasIterationLimit();
    puts("interaction raycast tests passed");
    return 0;
}
