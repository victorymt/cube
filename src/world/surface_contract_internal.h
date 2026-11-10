#ifndef VOXELCRAFT_WORLD_SURFACE_CONTRACT_INTERNAL_H
#define VOXELCRAFT_WORLD_SURFACE_CONTRACT_INTERNAL_H

#include "world/surface_contract.h"
#include "world/surface_topology_version.h"
#include "world/terrain.h"

struct Chunk;
struct ChunkSection;

bool SurfaceContractSampleFromTerrain(
    const SurfaceTerrainSample *terrain, bool hasPlanetBiome,
    PlanetBiome planetBiome, SurfaceContractSample *outSample);

typedef struct SurfaceCellRuntime {
    struct Chunk *chunk;
    struct ChunkSection *section;
    int sectionY;
    int localX;
    int localY;
    int localZ;
    BlockType block;
} SurfaceCellRuntime;

typedef struct SurfaceContractOps {
    bool (*address_is_valid)(SurfaceAddress address);
    bool (*address_equal)(SurfaceAddress a, SurfaceAddress b);
    uint32_t (*address_hash)(SurfaceAddress address);

    bool (*chunk_from_address)(const SurfaceContract *contract,
                               SurfaceAddress address,
                               SurfaceChunkHandle *outChunk);
    bool (*chunk_anchor_address)(const SurfaceContract *contract,
                                 SurfaceChunkHandle chunk,
                                 SurfaceAddress *outAddress);
    bool (*chunk_is_valid)(const SurfaceContract *contract,
                           SurfaceChunkHandle chunk);
    bool (*chunk_equal)(const SurfaceContract *contract,
                        SurfaceChunkHandle a, SurfaceChunkHandle b);
    bool (*chunk_neighbor)(const SurfaceContract *contract,
                           SurfaceChunkHandle chunk, SurfaceDirection direction,
                           SurfaceChunkHandle *outChunk,
                           SurfaceDirection *outReturnDirection);
    bool (*section_from_address)(const SurfaceContract *contract,
                                 SurfaceAddress address,
                                 SurfaceSectionHandle *outSection);
    bool (*section_is_valid)(const SurfaceContract *contract,
                             SurfaceSectionHandle section);
    bool (*locate_cell)(const SurfaceContract *contract,
                        SurfaceAddress address, int x, int y, int z,
                        SurfaceCellRuntime *outRuntime);
    bool (*step_cell)(const SurfaceContract *contract,
                      SurfaceAddress address, int x, int y, int z,
                      SurfaceDirection direction, SurfaceAddress *outAddress,
                      int *outX, int *outY, int *outZ,
                      SurfaceDirection *outReturnDirection);
    bool (*chunk_cell_address)(const SurfaceContract *contract,
                               const struct Chunk *chunk, int localX, int y,
                               int localZ, SurfaceAddress *outAddress);
    bool (*chunk_runtime)(const SurfaceContract *contract,
                          SurfaceChunkHandle chunk, struct Chunk **outChunk);

    bool (*cell_is_known)(const SurfaceContract *contract,
                          SurfaceAddress address);
    bool (*cell_read)(const SurfaceContract *contract, SurfaceAddress address,
                      WorldCell *outCell);
    bool (*cell_write)(const SurfaceContract *contract, SurfaceAddress address,
                       WorldCell cell);

    bool (*sample_at)(const SurfaceContract *contract, SurfaceAddress address,
                      SurfaceContractSample *outSample);

    bool (*neighbor)(const SurfaceContract *contract, SurfaceAddress address,
                     SurfaceDirection direction, SurfaceAddress *outAddress,
                     SurfaceDirection *outReturnDirection);
    void (*mark_address_dirty)(const SurfaceContract *contract,
                               SurfaceAddress address);

    bool (*same_region)(const SurfaceContract *contract, SurfaceAddress a,
                        SurfaceAddress b);
    SurfaceRegionHandle (*region_for_address)(const SurfaceContract *contract,
                                              SurfaceAddress address);

    bool (*coverage_collect)(const SurfaceContract *contract,
                             SurfaceAddress focus, double radius,
                             SurfaceCoverageCandidate *out, unsigned capacity,
                             SurfaceCoverageResult *outResult);
    bool (*coverage_status)(const SurfaceContract *contract,
                            SurfaceCoverageStatus *outStatus);
    bool (*coverage_update)(const SurfaceContract *contract,
                            SurfaceAddress focus, double radius,
                            unsigned submissionBudget,
                            SurfaceCoverageStatus *outStatus);
    bool (*coverage_observation)(
        const SurfaceContract *contract,
        SurfaceCoverageObservation *outObservation);
    bool (*coverage_admission_at)(
        const SurfaceContract *contract, unsigned index,
        SurfaceCoverageAdmission *outAdmission);
    bool (*coverage_admission_contains)(
        const SurfaceContract *contract, SurfaceChunkHandle chunk);

    bool supports_physical_raycast;
    bool supports_physical_pose;
    void (*release)(SurfaceContract *contract);
} SurfaceContractOps;

struct SurfaceContract {
    const SurfaceContractOps *ops;
    void *implementation;
    SurfaceTopologyVersion topology;
    uint32_t bodyId;
};

bool SurfaceContractMatchesTopology(const SurfaceContract *contract,
                                    SurfaceTopologyVersion topology,
                                    uint32_t bodyId);
bool SurfaceContractLocateCellRuntime(const SurfaceContract *contract,
                                      SurfaceAddress address, int x, int y,
                                      int z, SurfaceCellRuntime *outRuntime);
bool SurfaceContractStepCellRuntime(
    const SurfaceContract *contract, SurfaceAddress address, int x, int y,
    int z, SurfaceDirection direction, SurfaceAddress *outAddress,
    int *outX, int *outY, int *outZ, SurfaceDirection *outReturnDirection);
bool SurfaceContractChunkCellAddress(const SurfaceContract *contract,
                                     const struct Chunk *chunk, int localX,
                                     int y, int localZ,
                                     SurfaceAddress *outAddress);
bool SurfaceContractChunkRuntime(const SurfaceContract *contract,
                                 SurfaceChunkHandle chunk,
                                 struct Chunk **outChunk);
#endif
