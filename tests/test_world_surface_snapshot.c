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
            .topology = SURFACE_TOPOLOGY_MAP_V1,
            .seed = 0x134679acu,
            .originX = 27,
            .originZ = -41
        }
    };
    assert(WorldInstallSurfaceSnapshot(&home));
    assert(WorldCurrentSurfaceSnapshot()->context.active);

    assert(WorldInstallSurfaceSnapshot(NULL));
    AssertNoneSnapshot(WorldCurrentSurfaceSnapshot());
}

static void TestHomeSnapshotIsCopiedByValue(void)
{
    WorldSurfaceSnapshot source = {
        .context = {
            .active = true,
            .kind = WORLD_SURFACE_HOME,
            .topology = SURFACE_TOPOLOGY_MAP_V1,
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
    assert(WorldInstallSurfaceSnapshot(&source));
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
            .topology = SURFACE_TOPOLOGY_MAP_V1,
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
    assert(WorldInstallSurfaceSnapshot(&planet));

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

static void TestTopologyValidation(void)
{
    const SurfaceTopologyVersion none = { SURFACE_TOPOLOGY_NONE, 0u };
    const SurfaceTopologyVersion map = SURFACE_TOPOLOGY_MAP_V1;
    const SurfaceTopologyVersion cube = { SURFACE_TOPOLOGY_CUBE_GRID, 1u };
    assert(SurfaceTopologyIsKnown(none) && SurfaceTopologyIsRuntimeSupported(none));
    assert(SurfaceTopologyIsKnown(map) && SurfaceTopologyIsRuntimeSupported(map));
    assert(SurfaceTopologyIsKnown(cube) && !SurfaceTopologyIsRuntimeSupported(cube));
    assert(strcmp(SurfaceTopologyName(cube), "cube-grid") == 0);
    assert(!SurfaceTopologyIsKnown((SurfaceTopologyVersion){ SURFACE_TOPOLOGY_NONE, 1u }));
    assert(!SurfaceTopologyIsKnown((SurfaceTopologyVersion){ SURFACE_TOPOLOGY_LEGACY_MAP, 0u }));
    assert(!SurfaceTopologyIsKnown((SurfaceTopologyVersion){ SURFACE_TOPOLOGY_CUBE_GRID, 2u }));
    assert(!SurfaceTopologyIsKnown((SurfaceTopologyVersion){ (SurfaceTopologyKind)99, 1u }));

    WorldResetTopologyPolicy();
    WorldSurfaceTopologyPolicy policy = WorldCurrentTopologyPolicy();
    assert(policy.home.kind == map.kind && policy.home.version == 1u);
    assert(policy.planets.kind == map.kind && policy.planets.version == 1u);
    assert(WorldInstallTopologyPolicy(policy));
    policy.planets = cube;
    assert(!WorldInstallTopologyPolicy(policy));
    policy = WorldCurrentTopologyPolicy();
    policy.home = none;
    assert(!WorldInstallTopologyPolicy(policy));
    assert(WorldTopologyForSurface(WORLD_SURFACE_PLANET).kind == map.kind);

    WorldSurfaceSnapshot home = { .context = {
        .active = true, .kind = WORLD_SURFACE_HOME, .topology = map, .seed = 123u
    } };
    assert(WorldInstallSurfaceSnapshot(&home));
    uint64_t generation = WorldCurrentGeneration();
    const SurfaceTopologyVersion invalid[] = {
        none, cube, { SURFACE_TOPOLOGY_LEGACY_MAP, 2u }, { (SurfaceTopologyKind)99, 1u }
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        WorldSurfaceSnapshot bad = home;
        bad.context.topology = invalid[i];
        assert(!WorldInstallSurfaceSnapshot(&bad));
        assert(WorldCurrentGeneration() == generation);
        assert(memcmp(WorldCurrentSurfaceSnapshot(), &home, sizeof(home)) == 0);
    }
    home.context.active = false;
    assert(!WorldInstallSurfaceSnapshot(&home));
    assert(WorldCurrentGeneration() == generation);
    home.context.kind = WORLD_SURFACE_NONE;
    assert(!WorldInstallSurfaceSnapshot(&home));
    home.context.topology = none;
    assert(WorldInstallSurfaceSnapshot(&home));
    assert(WorldCurrentGeneration() != generation);
    assert(WorldCurrentSurfaceSnapshot()->context.topology.kind == SURFACE_TOPOLOGY_NONE);
}

int main(void)
{
    assert(WorldInstallSurfaceSnapshot(NULL));
    AssertNoneSnapshot(WorldCurrentSurfaceSnapshot());
    TestHomeSnapshotIsCopiedByValue();
    TestPlanetIdentityDoesNotAliasSeed();
    TestNullResetsSnapshot();
    TestTopologyValidation();
    assert(TerrainGenerationSetVersion(6));
    assert(WorldInstallSurfaceSnapshot(NULL));
    assert(TerrainGenerationVersion() == 6);
    assert(!TerrainGenerationSetVersion(0));
    assert(!TerrainGenerationSetVersion(TERRAIN_GENERATION_CURRENT + 1));
    assert(TerrainGenerationVersion() == 6);
    assert(TerrainGenerationSetVersion(TERRAIN_GENERATION_CURRENT));
    puts("world surface snapshot tests passed");
    return 0;
}
