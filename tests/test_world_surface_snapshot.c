#include "world/world_surface_snapshot.h"
#include "world/terrain_generation.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void AssertNoneSnapshot(const WorldSurfaceSnapshot *snapshot)
{
    assert(snapshot != NULL);
    assert(!snapshot->context.active);
    assert(snapshot->context.kind == WORLD_SURFACE_NONE);
    assert(snapshot->context.bodyId == 0u);
    assert(snapshot->context.seed == 0u);
    assert(snapshot->context.originX == 0);
    assert(snapshot->context.originZ == 0);
}

static void TestNullResetsSnapshot(void)
{
    WorldSurfaceSnapshot home = {
        .context = {
            .active = true,
            .kind = WORLD_SURFACE_HOME,
            .seed = 0x134679acu,
            .originX = 27,
            .originZ = -41
        }
    };
    WorldInstallSurfaceSnapshot(&home);
    assert(WorldCurrentSurfaceSnapshot()->context.active);

    WorldInstallSurfaceSnapshot(NULL);
    AssertNoneSnapshot(WorldCurrentSurfaceSnapshot());
}

static void TestHomeSnapshotIsCopiedByValue(void)
{
    WorldSurfaceSnapshot source = {
        .context = {
            .active = true,
            .kind = WORLD_SURFACE_HOME,
            .bodyId = 0u,
            .seed = 0x2468ace0u,
            .originX = 123,
            .originZ = -456
        },
        .planetProfile = {
            .seed = 0xa5a5a5a5u,
            .style = SOLAR_STYLE_TEMPERATE,
            .surfaceGravity = 1.0f
        }
    };
    WorldInstallSurfaceSnapshot(&source);
    memset(&source, 0, sizeof(source));

    const WorldSurfaceSnapshot *current = WorldCurrentSurfaceSnapshot();
    assert(current != NULL);
    assert(current->context.active);
    assert(current->context.kind == WORLD_SURFACE_HOME);
    assert(current->context.bodyId == 0u);
    assert(current->context.seed == 0x2468ace0u);
    assert(current->context.originX == 123);
    assert(current->context.originZ == -456);
    assert(current->planetProfile.seed == 0xa5a5a5a5u);
    assert(current->planetProfile.style == SOLAR_STYLE_TEMPERATE);
}

static void TestPlanetIdentityDoesNotAliasSeed(void)
{
    const WorldSurfaceSnapshot planet = {
        .context = {
            .active = true,
            .kind = WORLD_SURFACE_PLANET,
            .bodyId = 0x10203040u,
            .seed = 0x55667788u,
            .originX = -8192,
            .originZ = 4096
        },
        .planetProfile = {
            .seed = 0x55667788u,
            .canonicalBodyId = 4u,
            .style = SOLAR_STYLE_ICE,
            .surfaceGravity = 0.37f,
            .hasSolidSurface = true
        }
    };
    assert(planet.context.bodyId != planet.context.seed);
    WorldInstallSurfaceSnapshot(&planet);

    const WorldSurfaceSnapshot *current = WorldCurrentSurfaceSnapshot();
    assert(current->context.active);
    assert(current->context.kind == WORLD_SURFACE_PLANET);
    assert(current->context.bodyId == 0x10203040u);
    assert(current->context.seed == 0x55667788u);
    assert(current->context.originX == -8192);
    assert(current->context.originZ == 4096);
    assert(current->planetProfile.seed == current->context.seed);
    assert(current->planetProfile.canonicalBodyId == 4u);
    assert(current->planetProfile.style == SOLAR_STYLE_ICE);
    assert(fabsf(current->planetProfile.surfaceGravity - 0.37f) < 0.0001f);
}

int main(void)
{
    WorldInstallSurfaceSnapshot(NULL);
    AssertNoneSnapshot(WorldCurrentSurfaceSnapshot());
    TestHomeSnapshotIsCopiedByValue();
    TestPlanetIdentityDoesNotAliasSeed();
    TestNullResetsSnapshot();
    assert(TerrainGenerationSetVersion(6));
    WorldInstallSurfaceSnapshot(NULL);
    assert(TerrainGenerationVersion() == 6);
    assert(!TerrainGenerationSetVersion(0));
    assert(!TerrainGenerationSetVersion(8));
    assert(TerrainGenerationVersion() == 6);
    assert(TerrainGenerationSetVersion(TERRAIN_GENERATION_CURRENT));
    puts("world surface snapshot tests passed");
    return 0;
}
