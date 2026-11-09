#ifndef VOXELCRAFT_TESTS_SURFACE_CONTRACT_FIXTURE_H
#define VOXELCRAFT_TESTS_SURFACE_CONTRACT_FIXTURE_H

#include "world/surface_contract.h"

#include <stdbool.h>

/* Shared consumer fixture for contract tests. Backend setup and chunk reload
 * stay in the caller; these assertions must remain identical for both
 * adapters. */
typedef struct SurfaceContractFixture {
    SurfaceContract *contract;
    SurfaceAddress primary;
    SurfaceAddress twin;
    bool hasTwin;
    SurfaceAddress crossEdge;
    SurfaceDirection crossDirection;
    bool hasCrossEdge;
    bool crossEdgeChangesFace;
    void (*reload)(void *context);
    void *reloadContext;
} SurfaceContractFixture;

void SurfaceContractFixtureAssert(const SurfaceContractFixture *fixture);

#endif
