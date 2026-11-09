#ifndef VOXELCRAFT_TESTS_SURFACE_CONTRACT_TEST_STUB_H
#define VOXELCRAFT_TESTS_SURFACE_CONTRACT_TEST_STUB_H

#include "world/surface_contract_internal.h"
#include "world/surface_cube.h"
#include "world/world_surface_snapshot.h"

#include <assert.h>
#include <limits.h>

static bool TestContractNeighbor(const SurfaceContract *contract,
                                 SurfaceAddress address,
                                 SurfaceDirection direction,
                                 SurfaceAddress *outAddress,
                                 SurfaceDirection *outReturnDirection)
{
    if (!contract || !outAddress ||
        address.bodyId != contract->bodyId) return false;
    SurfaceAddress next = address;
    if (direction <= SURFACE_DIRECTION_V_PLUS) {
        SurfaceCubeEdge back;
        if (!SurfaceCubeCellNeighbor(address, (SurfaceCubeEdge)direction,
                                     &next, &back)) return false;
        if (outReturnDirection) *outReturnDirection = (SurfaceDirection)back;
    } else if (direction == SURFACE_DIRECTION_RADIAL_MINUS) {
        if (address.radial == INT_MIN) return false;
        next.radial--;
        if (outReturnDirection) *outReturnDirection =
            SURFACE_DIRECTION_RADIAL_PLUS;
    } else if (direction == SURFACE_DIRECTION_RADIAL_PLUS) {
        if (address.radial == INT_MAX) return false;
        next.radial++;
        if (outReturnDirection) *outReturnDirection =
            SURFACE_DIRECTION_RADIAL_MINUS;
    } else {
        return false;
    }
    *outAddress = next;
    return true;
}

static void TestContractReleaseNoop(SurfaceContract *contract)
{
    (void)contract;
}

static const SurfaceContractOps testSurfaceContractOps = {
    .neighbor = TestContractNeighbor,
    .release = TestContractReleaseNoop
};

static SurfaceContract testSurfaceContracts[64];
static unsigned testSurfaceContractCount;

static bool InstallTestSurfaceContract(uint32_t bodyId)
{
    if (testSurfaceContractCount >=
        sizeof(testSurfaceContracts) / sizeof(testSurfaceContracts[0])) {
        return false;
    }
    WorldSurfaceSnapshot snapshot = {
        .context = {
            .active = true,
            .kind = bodyId == 0 ? WORLD_SURFACE_HOME : WORLD_SURFACE_PLANET,
            .topology = SURFACE_TOPOLOGY_MAP_V1,
            .bodyId = bodyId,
            .seed = bodyId ^ UINT32_C(0x51a7e)
        }
    };
    SurfaceContract *contract =
        &testSurfaceContracts[testSurfaceContractCount++];
    *contract = (SurfaceContract){
        .ops = &testSurfaceContractOps,
        .implementation = contract,
        .topology = snapshot.context.topology,
        .bodyId = bodyId
    };
    return WorldInstallSurfaceSnapshotWithContract(&snapshot, contract);
}

#endif
