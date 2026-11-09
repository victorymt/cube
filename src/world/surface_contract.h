#ifndef VOXELCRAFT_WORLD_SURFACE_CONTRACT_H
#define VOXELCRAFT_WORLD_SURFACE_CONTRACT_H

#include "world/surface_types.h"
#include "world/terrain_types.h"
#include "world/world_cell.h"

#include <stdbool.h>
#include <stdint.h>

/* Consumer-facing surface operations. These values and calls describe cells,
 * chunks, samples and neighbours without exposing which storage topology is
 * active. Backend-specific identities stay inside opaque handles. */
typedef struct SurfaceContract SurfaceContract;

typedef struct SurfaceChunkHandle {
    uint64_t opaque[5];
} SurfaceChunkHandle;

typedef struct SurfaceSectionHandle {
    SurfaceChunkHandle chunk;
    int sectionY;
} SurfaceSectionHandle;

typedef struct SurfaceRegionHandle {
    uint64_t opaque[2];
} SurfaceRegionHandle;

typedef enum SurfaceDirection {
    SURFACE_DIRECTION_U_MINUS = 0,
    SURFACE_DIRECTION_U_PLUS,
    SURFACE_DIRECTION_V_MINUS,
    SURFACE_DIRECTION_V_PLUS,
    SURFACE_DIRECTION_RADIAL_MINUS,
    SURFACE_DIRECTION_RADIAL_PLUS
} SurfaceDirection;

typedef struct SurfaceContractSample {
    float elevation;
    float seaLevel;
    float waterSurface;
    int seabedY;
    int waterDepth;
    Biome biome;
} SurfaceContractSample;

typedef struct SurfaceCoverageCandidate {
    SurfaceChunkHandle chunk;
    double centerDistance;
    double minimumDistance;
} SurfaceCoverageCandidate;

typedef struct SurfaceCoverageResult {
    unsigned requested;
    unsigned admitted;
    bool saturated;
} SurfaceCoverageResult;

typedef struct SurfaceCoverageStatus {
    unsigned requested;
    unsigned admitted;
    unsigned ready;
    unsigned pending;
    unsigned missing;
    unsigned submitted;
    unsigned released;
    bool saturated;
} SurfaceCoverageStatus;

/* Releases a contract owned by the caller. The world snapshot module releases
 * the previously installed contract after a successful transactional swap. */
void SurfaceContractRelease(SurfaceContract *contract);

/* Metadata for composition roots and diagnostics, not gameplay routing. */
bool SurfaceContractIsValid(const SurfaceContract *contract);
uint32_t SurfaceContractBodyId(const SurfaceContract *contract);
const char *SurfaceContractBackendName(const SurfaceContract *contract);

/* Stable cell identity. These calls are topology independent. */
bool SurfaceContractAddressIsValid(SurfaceAddress address);
bool SurfaceContractAddressEqual(SurfaceAddress a, SurfaceAddress b);
uint32_t SurfaceContractAddressHash(SurfaceAddress address);

/* Opaque chunk/section access. A handle is only meaningful for the contract
 * that produced it. */
bool SurfaceContractChunkFromAddress(const SurfaceContract *contract,
                                     SurfaceAddress address,
                                     SurfaceChunkHandle *outChunk);
bool SurfaceContractChunkAnchorAddress(const SurfaceContract *contract,
                                       SurfaceChunkHandle chunk,
                                       SurfaceAddress *outAddress);
bool SurfaceContractChunkIsValid(const SurfaceContract *contract,
                                 SurfaceChunkHandle chunk);
bool SurfaceContractChunkEqual(const SurfaceContract *contract,
                               SurfaceChunkHandle a, SurfaceChunkHandle b);
uint32_t SurfaceContractChunkHash(const SurfaceContract *contract,
                                  SurfaceChunkHandle chunk);
bool SurfaceContractSectionFromAddress(const SurfaceContract *contract,
                                       SurfaceAddress address,
                                       SurfaceSectionHandle *outSection);
bool SurfaceContractSectionIsValid(const SurfaceContract *contract,
                                   SurfaceSectionHandle section);

/* Semantic horizontal and radial adjacency. A face-edge transition is handled
 * by the adapter, so callers never parse a face or a map coordinate. */
bool SurfaceContractNeighbor(const SurfaceContract *contract,
                             SurfaceAddress address, SurfaceDirection direction,
                             SurfaceAddress *outAddress,
                             SurfaceDirection *outReturnDirection);
bool SurfaceContractChunkNeighbor(const SurfaceContract *contract,
                                  SurfaceChunkHandle chunk,
                                  SurfaceDirection direction,
                                  SurfaceChunkHandle *outChunk,
                                  SurfaceDirection *outReturnDirection);
void SurfaceContractMarkAddressDirty(const SurfaceContract *contract,
                                     SurfaceAddress address);

/* Cell reads distinguish unresolved terrain from known air. Writes reject
 * invalid cells and backends that cannot represent the requested state. */
bool SurfaceContractCellIsKnown(const SurfaceContract *contract,
                                SurfaceAddress address);
bool SurfaceContractCellRead(const SurfaceContract *contract,
                             SurfaceAddress address, WorldCell *outCell);
bool SurfaceContractCellWrite(const SurfaceContract *contract,
                              SurfaceAddress address, WorldCell cell);

/* Column sampling gives consumers one semantic source for ground, water and
 * biome data. */
bool SurfaceContractSampleAt(const SurfaceContract *contract,
                             SurfaceAddress address,
                             SurfaceContractSample *outSample);
bool SurfaceContractTerrainSampleAt(const SurfaceContract *contract,
                                    SurfaceAddress address,
                                    float *outElevation, float *outSeaLevel);
bool SurfaceContractWaterSampleAt(const SurfaceContract *contract,
                                  SurfaceAddress address,
                                  int *outSeabedY, int *outWaterDepth);
bool SurfaceContractBiomeAt(const SurfaceContract *contract,
                            SurfaceAddress address, Biome *outBiome);

/* Body and semantic-region identity. */
bool SurfaceContractSameRegion(const SurfaceContract *contract,
                               SurfaceAddress a, SurfaceAddress b);
SurfaceRegionHandle SurfaceContractRegionForAddress(
    const SurfaceContract *contract, SurfaceAddress address);

/* Optional physical operations. Collection returns opaque chunk handles in
 * the same nearest-first order used by admission. Capabilities are answered by
 * the active backend so consumers can install generic picking/streaming hooks
 * without branching on the topology kind. */
bool SurfaceContractSupportsPhysicalCoverage(const SurfaceContract *contract);
bool SurfaceContractSupportsPhysicalRaycast(const SurfaceContract *contract);
bool SurfaceContractSupportsPhysicalPose(const SurfaceContract *contract);
bool SurfaceContractCoverageCollect(const SurfaceContract *contract,
                                    SurfaceAddress focus, double radius,
                                    SurfaceCoverageCandidate *out,
                                    unsigned capacity,
                                    SurfaceCoverageResult *outResult);
bool SurfaceContractCoverageStatus(const SurfaceContract *contract,
                                   SurfaceCoverageStatus *outStatus);
bool SurfaceContractCoverageUpdate(const SurfaceContract *contract,
                                   SurfaceAddress focus, double radius,
                                   unsigned submissionBudget,
                                   SurfaceCoverageStatus *outStatus);

#endif
