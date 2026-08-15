#include "ship_locator.h"
#include "raymath.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static ShipLocatorContext HomeContext(void)
{
    return (ShipLocatorContext){ .dimension = WORLD_DIMENSION_HOME };
}

static void TestLocalAndRemoteResolution(void)
{
    ShipLocatorReset();
    assert(!ShipLocatorHasTarget());
    assert(ShipLocatorRecordParked(HomeContext(), 12, 4, -8, "Homeworld"));

    ShipLocatorTarget local = { 0 };
    assert(ShipLocatorResolve(HomeContext(), (Vector3){ 0.5f, 4.5f, -7.5f },
                              &local));
    assert(local.status == SHIP_LOCATOR_TARGET_LOCAL);
    assert(local.blockX == 12 && local.blockY == 4 && local.blockZ == -8);
    assert(fabsf(local.distance - 12.0f) < 0.0001f);
    assert(strcmp(local.location, "Homeworld") == 0);

    ShipLocatorContext planet = {
        .dimension = WORLD_DIMENSION_PLANET,
        .surfaceId = 0x1234u
    };
    ShipLocatorTarget remote = { 0 };
    assert(ShipLocatorResolve(planet, Vector3Zero(), &remote));
    assert(remote.status == SHIP_LOCATOR_TARGET_REMOTE);
    assert(strcmp(remote.location, "Homeworld") == 0);

    assert(!ShipLocatorRemoveIfMatches(HomeContext(), 11, 4, -8));
    assert(ShipLocatorRemoveIfMatches(HomeContext(), 12, 4, -8));
    assert(!ShipLocatorHasTarget());

    assert(ShipLocatorRecordParked(
        HomeContext(), 2, SURFACE_MIN_Y, 3, "Deep Homeworld"));
    assert(!ShipLocatorRecordParked(
        HomeContext(), 2, SURFACE_MIN_Y - 1, 3, "Too deep"));
    assert(!ShipLocatorRecordParked(
        HomeContext(), 2, SURFACE_MAX_Y_EXCLUSIVE, 3, "Too high"));
    ShipLocatorReset();
}

static void TestPlanetIdentityAndSpaceRebase(void)
{
    ShipLocatorContext planetA = {
        .dimension = WORLD_DIMENSION_PLANET,
        .surfaceId = 44u
    };
    ShipLocatorContext planetB = planetA;
    planetB.surfaceId = 45u;
    assert(ShipLocatorRecordParked(planetA, -3, 7, 19, "Kepler b"));

    ShipLocatorTarget target = { 0 };
    assert(ShipLocatorResolve(planetB, Vector3Zero(), &target));
    assert(target.status == SHIP_LOCATOR_TARGET_REMOTE);
    assert(ShipLocatorResolve(planetA, Vector3Zero(), &target));
    assert(target.status == SHIP_LOCATOR_TARGET_LOCAL);

    ShipLocatorContext firstFrame = {
        .dimension = WORLD_DIMENSION_SPACE,
        .spaceOriginX = 12000,
        .spaceOriginZ = -8000
    };
    assert(ShipLocatorRecordParked(firstFrame, 25, SPACE_LAYER_Y + 2, -40,
                                   "Deep space"));
    ShipLocatorContext rebased = firstFrame;
    rebased.spaceOriginX += 3000;
    rebased.spaceOriginZ -= 6000;
    assert(ShipLocatorResolve(rebased, Vector3Zero(), &target));
    assert(target.status == SHIP_LOCATOR_TARGET_LOCAL);
    assert(target.blockX == -2975);
    assert(target.blockZ == 5960);
}

static void TestPersistenceAndAtomicFailure(void)
{
    ShipLocatorReset();
    FILE *emptyState = tmpfile();
    assert(emptyState && ShipLocatorSaveState(emptyState));
    rewind(emptyState);
    ShipLocatorRecord emptyRecord = { .deployed = true };
    assert(ShipLocatorReadState(emptyState, &emptyRecord));
    fclose(emptyState);
    assert(!emptyRecord.deployed);

    assert(ShipLocatorRecordParked(HomeContext(), 7, 3, 9, "Homeworld"));
    FILE *file = tmpfile();
    assert(file && ShipLocatorSaveState(file));
    rewind(file);
    ShipLocatorReset();
    assert(ShipLocatorLoadState(file));
    fclose(file);
    ShipLocatorRecord restored = ShipLocatorGetRecord();
    assert(restored.deployed && restored.x == 7 && restored.y == 3 && restored.z == 9);

    FILE *truncated = tmpfile();
    assert(truncated);
    unsigned char invalid = 1u;
    assert(fwrite(&invalid, sizeof(invalid), 1, truncated) == 1);
    rewind(truncated);
    assert(!ShipLocatorLoadState(truncated));
    fclose(truncated);
    ShipLocatorRecord afterFailure = ShipLocatorGetRecord();
    assert(memcmp(&restored, &afterFailure, sizeof(restored)) == 0);

    ShipLocatorRecord invalidRecord = restored;
    invalidRecord.dimension = WORLD_DIMENSION_PLANET;
    invalidRecord.surfaceId = 0u;
    assert(!ShipLocatorSetRecord(&invalidRecord));
    afterFailure = ShipLocatorGetRecord();
    assert(memcmp(&restored, &afterFailure, sizeof(restored)) == 0);
}

static void TestLegacySpaceLayerMigration(void)
{
    ShipLocatorContext context = { .dimension = WORLD_DIMENSION_SPACE };
    assert(ShipLocatorRecordParked(context, 4, SPACE_LAYER_Y + 12, -6,
                                   "Legacy orbit"));
    FILE *file = tmpfile();
    assert(file && ShipLocatorSaveState(file));

    long yOffset = (long)sizeof(uint8_t) + (long)sizeof(uint32_t) * 2L +
                   (long)sizeof(int32_t);
    assert(fseek(file, yOffset, SEEK_SET) == 0);
    int32_t legacyY = 112;
    assert(fwrite(&legacyY, sizeof(legacyY), 1, file) == 1);
    rewind(file);

    ShipLocatorRecord migrated = { 0 };
    assert(ShipLocatorReadStateForSpaceLayer(file, &migrated, 100));
    assert(migrated.deployed);
    assert(migrated.y == SPACE_LAYER_Y + 12);
    fclose(file);
}

static void AssertInside(Vector2 point, int width, int height, float margin)
{
    assert(isfinite(point.x) && isfinite(point.y));
    assert(point.x >= margin - 0.01f && point.x <= (float)width - margin + 0.01f);
    assert(point.y >= margin - 0.01f && point.y <= (float)height - margin + 0.01f);
}

static void TestMarkerLayout(void)
{
    ShipLocatorMarkerLayout center = ShipLocatorMarkerLayoutEvaluate(
        (Vector2){ 640.0f, 360.0f }, false, 1280, 720, 46.0f);
    assert(center.visible && center.onScreen);
    assert(center.position.x == 640.0f && center.position.y == 360.0f);

    const Vector2 outside[] = {
        { -500.0f, 360.0f }, { 1800.0f, 360.0f },
        { 640.0f, -500.0f }, { 640.0f, 1200.0f }
    };
    for (unsigned i = 0; i < sizeof(outside) / sizeof(outside[0]); i++) {
        ShipLocatorMarkerLayout edge = ShipLocatorMarkerLayoutEvaluate(
            outside[i], false, 1280, 720, 46.0f);
        assert(edge.visible && !edge.onScreen);
        AssertInside(edge.position, 1280, 720, 46.0f);
    }

    ShipLocatorMarkerLayout behind = ShipLocatorMarkerLayoutEvaluate(
        (Vector2){ 640.0f, 360.0f }, true, 1280, 720, 46.0f);
    assert(behind.visible && !behind.onScreen);
    AssertInside(behind.position, 1280, 720, 46.0f);

    ShipLocatorMarkerLayout invalid = ShipLocatorMarkerLayoutEvaluate(
        (Vector2){ NAN, 0.0f }, false, 1280, 720, 46.0f);
    assert(!invalid.visible);
}

int main(void)
{
    TestLocalAndRemoteResolution();
    TestPlanetIdentityAndSpaceRebase();
    TestPersistenceAndAtomicFailure();
    TestLegacySpaceLayerMigration();
    TestMarkerLayout();
    puts("ship locator tests passed");
    return 0;
}
