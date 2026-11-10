#ifndef VOXELCRAFT_WORLD_ENVIRONMENT_H
#define VOXELCRAFT_WORLD_ENVIRONMENT_H

#include "world/surface_topology_version.h"
#include "world/surface_contract.h"
#include "world/terrain_types.h"
#include "world/world_cell.h"
#include "world/world_location.h"

#include <stdint.h>
#include "world/surface_topology.h"

struct Chunk;

// The active environment describes where the player currently lives. The
// block region is kept separate because the space layer overlaps the ascent
// corridor in the local coordinate system.
typedef enum WorldBlockRegion {
    WORLD_BLOCK_REGION_NONE = 0,
    WORLD_BLOCK_REGION_SURFACE,
    WORLD_BLOCK_REGION_NETHER,
    WORLD_BLOCK_REGION_SPACE
} WorldBlockRegion;

// Surface context deliberately excludes Nether: Nether overlays the Home
// surface and callers that need the active surface must continue to treat it
// as Home. WorldDimension remains the position/presentation-facing concept.
typedef enum WorldSurfaceKind {
    WORLD_SURFACE_NONE = 0,
    WORLD_SURFACE_HOME,
    WORLD_SURFACE_PLANET
} WorldSurfaceKind;

typedef struct WorldSurfaceContext {
    bool active;
    WorldSurfaceKind kind;
    SurfaceTopologyVersion topology;
    uint32_t bodyId;
    uint32_t seed;
    int originX;
    int originZ;
} WorldSurfaceContext;

typedef struct WorldSurfaceRebaseEvent {
    bool valid;
    uint64_t sequence;
    uint32_t bodyId;
    Vector2 previous;
    Vector2 canonical;
    float northDirection;
} WorldSurfaceRebaseEvent;

typedef struct WorldSurfaceCellRuntime {
    struct Chunk *chunk;
    int sectionY;
    int localX;
    int localY;
    int localZ;
    BlockType block;
} WorldSurfaceCellRuntime;

// Dimension selection is explicit; the position-aware form remains for
// callers that also use altitude for presentation decisions.
WorldDimension WorldCurrentDimension(void);
WorldDimension WorldCurrentDimensionAt(float y);
bool WorldNetherIsActive(void);
void WorldSetNetherActive(bool active);
const char *WorldDimensionName(WorldDimension dimension);
WorldBlockRegion WorldBlockRegionAt(int y);

bool WorldIsSurfaceActive(void);
WorldSurfaceContext WorldCurrentSurfaceContext(void);
bool WorldIsSpaceActive(void);
bool WorldIsSurfaceDimension(WorldDimension dimension);
int WorldSurfaceMinY(void);
int WorldSurfaceMaxYExclusive(void);
bool WorldCanAccessBlockY(int y);
float WorldGravityScale(void);
bool WorldSurfaceSampleAt(int x, int z, SurfaceContractSample *outSample);
int WorldSurfaceHeightAt(int x, int z);
/* Consumer-facing cell access for entity, ecology and spawn code. The address
 * is derived from the active surface snapshot; no map chunk lookup escapes. */
bool WorldSurfaceCellAt(int x, int y, int z, WorldCell *outCell);
bool WorldSurfaceCellKnownAt(int x, int y, int z);
bool WorldSurfaceBiomeAt(int x, int z, Biome *outBiome);
bool WorldSurfacePlanetBiomeAt(int x, int z, PlanetBiome *outBiome);
bool WorldSurfaceBathymetryAt(int x, int z, BathymetrySample *outSample);
bool WorldSurfaceCellRuntimeAt(SurfaceAddress address,
                               WorldSurfaceCellRuntime *outRuntime);
bool WorldProceduralWaterSurfaceAt(int x, int z, float *outSurfaceY);
bool WorldIsProceduralOceanWaterAt(int x, int y, int z);
uint32_t WorldCurrentSurfaceId(void);
int WorldSurfaceMapOriginX(void);
int WorldSurfaceMapOriginZ(void);
bool WorldCanonicalizeSurfacePose(Vector3 *position, Vector3 *velocity,
                                  float *yaw);
void WorldResetSurfaceRebaseEvent(void);
WorldSurfaceRebaseEvent WorldLastSurfaceRebaseEvent(void);

#endif
