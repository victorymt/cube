#ifndef VOXELCRAFT_WORLD_SURFACE_SNAPSHOT_H
#define VOXELCRAFT_WORLD_SURFACE_SNAPSHOT_H

#include "space/planet_profile.h"
#include "world/surface_contract.h"
#include "world/world_environment.h"

typedef struct WorldSurfaceSnapshot {
    WorldSurfaceContext context;
    PlanetProfile planetProfile;
} WorldSurfaceSnapshot;

// Surface transitions install an immutable-by-convention value snapshot after
// chunk generation has drained. Passing NULL clears it to the neutral value.
// Invalid/unsupported topology leaves both the snapshot and generation intact.
bool WorldInstallSurfaceSnapshot(const WorldSurfaceSnapshot *snapshot);
// Transactional variant used by the composition root. On success the world
// owns contract and releases the previous one only after the swap. On failure
// the caller retains the new contract and no observable world state changes.
bool WorldInstallSurfaceSnapshotWithContract(
    const WorldSurfaceSnapshot *snapshot, SurfaceContract *contract);
const WorldSurfaceSnapshot *WorldCurrentSurfaceSnapshot(void);
const SurfaceContract *WorldCurrentSurfaceContract(void);
uint64_t WorldCurrentGeneration(void);
// Returns the generation a successful next install would publish. It never
// mutates the world and never returns zero.
uint64_t WorldNextSurfaceGeneration(void);

// Main-thread policy, installed only after draining chunk workers. Every planet
// inherits the same policy; there are no per-body overrides in this stage.
WorldSurfaceTopologyPolicy WorldCurrentTopologyPolicy(void);
bool WorldInstallTopologyPolicy(WorldSurfaceTopologyPolicy policy);
void WorldResetTopologyPolicy(void);
SurfaceTopologyVersion WorldTopologyForSurface(WorldSurfaceKind kind);

/* Runtime gate for the cube grid. Off by default: with the gate closed the
 * runtime accepts only the legacy map grid, exactly as before this API
 * existed. Opening it permits a cube-grid policy and snapshot to install, so
 * callers must drain chunk workers and rebuild all dependent state first.
 * This gate does not switch the default policy; it only stops the hard
 * rejection of an explicitly requested cube world. */
bool WorldCubeRuntimeEnabled(void);
void WorldSetCubeRuntimeEnabled(bool enabled);

#endif
