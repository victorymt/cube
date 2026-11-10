#include "world/surface_contract.h"
#include "world/surface_contract_internal.h"

#include <math.h>
#include <stddef.h>

bool SurfaceContractSampleFromTerrain(
    const SurfaceTerrainSample *terrain, bool hasPlanetBiome,
    PlanetBiome planetBiome, SurfaceContractSample *outSample)
{
    if (!terrain || !outSample) return false;
    BathymetrySample bathymetry = terrain->bathymetry;
    float waterSurface = bathymetry.waterDepth > 0
        ? (float)bathymetry.seaLevel + 1.0f : terrain->seaLevel;
    if (bathymetry.waterDepth <= 0 && terrain->landform.hasWater) {
        waterSurface = terrain->landform.waterLevel;
        bathymetry.seabedY = (int)lroundf(terrain->elevation);
        bathymetry.waterDepth = (int)fmaxf(
            ceilf(terrain->landform.waterLevel - terrain->elevation), 0.0f);
        bathymetry.zone = BATHYMETRY_ZONE_COAST;
        bathymetry.material = BATHYMETRY_MATERIAL_SEDIMENT;
    }
    *outSample = (SurfaceContractSample){
        .elevation = terrain->elevation,
        .seaLevel = terrain->seaLevel,
        .waterSurface = waterSurface,
        .slope = terrain->slope,
        .seabedY = bathymetry.seabedY,
        .waterDepth = bathymetry.waterDepth,
        .bathymetryZone = bathymetry.zone,
        .bathymetryMaterial = bathymetry.material,
        .biome = terrain->biome,
        .planetBiome = planetBiome,
        .hasPlanetBiome = hasPlanetBiome
    };
    return true;
}

static bool ContractIsUsable(const SurfaceContract *contract)
{
    return contract && contract->ops && contract->implementation &&
           contract->topology.kind != SURFACE_TOPOLOGY_NONE;
}

static bool OpsChunkIsValid(const SurfaceContract *contract,
                            SurfaceChunkHandle chunk)
{
    return ContractIsUsable(contract) && contract->ops->chunk_is_valid &&
           contract->ops->chunk_is_valid(contract, chunk);
}

void SurfaceContractRelease(SurfaceContract *contract)
{
    if (!contract) return;
    if (contract->ops && contract->ops->release) {
        contract->ops->release(contract);
    }
}

bool SurfaceContractIsValid(const SurfaceContract *contract)
{
    return ContractIsUsable(contract);
}

uint32_t SurfaceContractBodyId(const SurfaceContract *contract)
{
    return ContractIsUsable(contract) ? contract->bodyId : 0u;
}

const char *SurfaceContractBackendName(const SurfaceContract *contract)
{
    return ContractIsUsable(contract)
        ? SurfaceTopologyName(contract->topology) : "none";
}

bool SurfaceContractAddressIsValid(SurfaceAddress address)
{
    return address.face >= SURFACE_FACE_POS_X &&
           address.face < SURFACE_FACE_COUNT &&
           address.u >= 0 && address.u < SURFACE_FACE_BLOCKS &&
           address.v >= 0 && address.v < SURFACE_FACE_BLOCKS;
}

bool SurfaceContractAddressEqual(SurfaceAddress a, SurfaceAddress b)
{
    return a.bodyId == b.bodyId && a.face == b.face && a.u == b.u &&
           a.v == b.v && a.radial == b.radial;
}

uint32_t SurfaceContractAddressHash(SurfaceAddress address)
{
    const uint32_t fields[] = {
        address.bodyId, (uint32_t)address.face, (uint32_t)address.u,
        (uint32_t)address.v, (uint32_t)address.radial
    };
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            hash = (hash ^ ((fields[i] >> shift) & 255u)) * 16777619u;
        }
    }
    return hash;
}

bool SurfaceContractChunkFromAddress(const SurfaceContract *contract,
                                     SurfaceAddress address,
                                     SurfaceChunkHandle *outChunk)
{
    return ContractIsUsable(contract) && outChunk &&
           contract->ops->chunk_from_address &&
           contract->ops->chunk_from_address(contract, address, outChunk);
}

bool SurfaceContractChunkAnchorAddress(const SurfaceContract *contract,
                                       SurfaceChunkHandle chunk,
                                       SurfaceAddress *outAddress)
{
    return ContractIsUsable(contract) && outAddress &&
           contract->ops->chunk_anchor_address &&
           contract->ops->chunk_anchor_address(contract, chunk, outAddress);
}

bool SurfaceContractChunkIsValid(const SurfaceContract *contract,
                                 SurfaceChunkHandle chunk)
{
    return OpsChunkIsValid(contract, chunk);
}

bool SurfaceContractChunkEqual(const SurfaceContract *contract,
                               SurfaceChunkHandle a, SurfaceChunkHandle b)
{
    return OpsChunkIsValid(contract, a) && OpsChunkIsValid(contract, b) &&
           contract->ops->chunk_equal &&
           contract->ops->chunk_equal(contract, a, b);
}

uint32_t SurfaceContractChunkHash(const SurfaceContract *contract,
                                  SurfaceChunkHandle chunk)
{
    if (!OpsChunkIsValid(contract, chunk)) return 0u;
    uint32_t hash = 2166136261u;
    const unsigned char *bytes = (const unsigned char *)chunk.opaque;
    for (unsigned i = 0; i < sizeof(chunk.opaque); i++) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

bool SurfaceContractSectionFromAddress(const SurfaceContract *contract,
                                       SurfaceAddress address,
                                       SurfaceSectionHandle *outSection)
{
    return ContractIsUsable(contract) && outSection &&
           contract->ops->section_from_address &&
           contract->ops->section_from_address(contract, address, outSection);
}

bool SurfaceContractSectionIsValid(const SurfaceContract *contract,
                                   SurfaceSectionHandle section)
{
    return ContractIsUsable(contract) && contract->ops->section_is_valid &&
           contract->ops->section_is_valid(contract, section);
}

bool SurfaceContractLocateCellRuntime(const SurfaceContract *contract,
                                      SurfaceAddress address, int x, int y,
                                      int z, SurfaceCellRuntime *outRuntime)
{
    return ContractIsUsable(contract) && outRuntime &&
           contract->ops->locate_cell &&
           contract->ops->locate_cell(contract, address, x, y, z, outRuntime);
}

bool SurfaceContractStepCellRuntime(
    const SurfaceContract *contract, SurfaceAddress address, int x, int y,
    int z, SurfaceDirection direction, SurfaceAddress *outAddress,
    int *outX, int *outY, int *outZ, SurfaceDirection *outReturnDirection)
{
    return ContractIsUsable(contract) && outAddress &&
           contract->ops->step_cell &&
           contract->ops->step_cell(contract, address, x, y, z, direction,
                                    outAddress, outX, outY, outZ,
                                    outReturnDirection);
}

bool SurfaceContractChunkCellAddress(const SurfaceContract *contract,
                                     const struct Chunk *chunk, int localX,
                                     int y, int localZ,
                                     SurfaceAddress *outAddress)
{
    return ContractIsUsable(contract) && chunk && outAddress &&
           contract->ops->chunk_cell_address &&
           contract->ops->chunk_cell_address(
               contract, chunk, localX, y, localZ, outAddress);
}

bool SurfaceContractChunkRuntime(const SurfaceContract *contract,
                                 SurfaceChunkHandle chunk,
                                 struct Chunk **outChunk)
{
    return ContractIsUsable(contract) && outChunk &&
           contract->ops->chunk_runtime &&
           contract->ops->chunk_runtime(contract, chunk, outChunk);
}

bool SurfaceContractNeighbor(const SurfaceContract *contract,
                             SurfaceAddress address, SurfaceDirection direction,
                             SurfaceAddress *outAddress,
                             SurfaceDirection *outReturnDirection)
{
    return ContractIsUsable(contract) && outAddress &&
           contract->ops->neighbor &&
           contract->ops->neighbor(contract, address, direction, outAddress,
                                   outReturnDirection);
}

bool SurfaceContractChunkNeighbor(const SurfaceContract *contract,
                                  SurfaceChunkHandle chunk,
                                  SurfaceDirection direction,
                                  SurfaceChunkHandle *outChunk,
                                  SurfaceDirection *outReturnDirection)
{
    return OpsChunkIsValid(contract, chunk) && outChunk &&
           contract->ops->chunk_neighbor &&
           contract->ops->chunk_neighbor(contract, chunk, direction, outChunk,
                                         outReturnDirection);
}

void SurfaceContractMarkAddressDirty(const SurfaceContract *contract,
                                     SurfaceAddress address)
{
    if (ContractIsUsable(contract) && contract->ops->mark_address_dirty) {
        contract->ops->mark_address_dirty(contract, address);
    }
}

bool SurfaceContractCellIsKnown(const SurfaceContract *contract,
                                SurfaceAddress address)
{
    return ContractIsUsable(contract) && contract->ops->cell_is_known &&
           contract->ops->cell_is_known(contract, address);
}

bool SurfaceContractCellRead(const SurfaceContract *contract,
                             SurfaceAddress address, WorldCell *outCell)
{
    return ContractIsUsable(contract) && outCell && contract->ops->cell_read &&
           contract->ops->cell_read(contract, address, outCell);
}

bool SurfaceContractCellWrite(const SurfaceContract *contract,
                              SurfaceAddress address, WorldCell cell)
{
    return ContractIsUsable(contract) && contract->ops->cell_write &&
           contract->ops->cell_write(contract, address, cell);
}

bool SurfaceContractSampleAt(const SurfaceContract *contract,
                             SurfaceAddress address,
                             SurfaceContractSample *outSample)
{
    return ContractIsUsable(contract) && outSample &&
           contract->ops->sample_at &&
           contract->ops->sample_at(contract, address, outSample);
}

bool SurfaceContractTerrainSampleAt(const SurfaceContract *contract,
                                    SurfaceAddress address,
                                    float *outElevation, float *outSeaLevel)
{
    SurfaceContractSample sample;
    if ((!outElevation && !outSeaLevel) ||
        !SurfaceContractSampleAt(contract, address, &sample)) return false;
    if (outElevation) *outElevation = sample.elevation;
    if (outSeaLevel) *outSeaLevel = sample.seaLevel;
    return true;
}

bool SurfaceContractWaterSampleAt(const SurfaceContract *contract,
                                  SurfaceAddress address,
                                  int *outSeabedY, int *outWaterDepth)
{
    SurfaceContractSample sample;
    if ((!outSeabedY && !outWaterDepth) ||
        !SurfaceContractSampleAt(contract, address, &sample)) return false;
    if (outSeabedY) *outSeabedY = sample.seabedY;
    if (outWaterDepth) *outWaterDepth = sample.waterDepth;
    return true;
}

bool SurfaceContractBiomeAt(const SurfaceContract *contract,
                            SurfaceAddress address, Biome *outBiome)
{
    SurfaceContractSample sample;
    if (!outBiome || !SurfaceContractSampleAt(contract, address, &sample)) {
        return false;
    }
    *outBiome = sample.biome;
    return true;
}

bool SurfaceContractSameRegion(const SurfaceContract *contract,
                               SurfaceAddress a, SurfaceAddress b)
{
    if (!ContractIsUsable(contract) || !contract->ops->same_region) return false;
    return contract->ops->same_region(contract, a, b);
}

SurfaceRegionHandle SurfaceContractRegionForAddress(
    const SurfaceContract *contract, SurfaceAddress address)
{
    if (!ContractIsUsable(contract) || !contract->ops->region_for_address) {
        return (SurfaceRegionHandle){ 0 };
    }
    return contract->ops->region_for_address(contract, address);
}

bool SurfaceContractSupportsPhysicalCoverage(const SurfaceContract *contract)
{
    return ContractIsUsable(contract) &&
           contract->ops->coverage_collect &&
           contract->ops->coverage_status &&
           contract->ops->coverage_update;
}

bool SurfaceContractSupportsPhysicalRaycast(const SurfaceContract *contract)
{
    return ContractIsUsable(contract) &&
           contract->ops->supports_physical_raycast;
}

bool SurfaceContractSupportsPhysicalPose(const SurfaceContract *contract)
{
    return ContractIsUsable(contract) &&
           contract->ops->supports_physical_pose;
}

bool SurfaceContractCoverageCollect(const SurfaceContract *contract,
                                    SurfaceAddress focus, double radius,
                                    SurfaceCoverageCandidate *out,
                                    unsigned capacity,
                                    SurfaceCoverageResult *outResult)
{
    return ContractIsUsable(contract) && out && outResult && capacity &&
           contract->ops->coverage_collect &&
           contract->ops->coverage_collect(contract, focus, radius, out,
                                           capacity, outResult);
}

bool SurfaceContractCoverageStatus(const SurfaceContract *contract,
                                   SurfaceCoverageStatus *outStatus)
{
    return ContractIsUsable(contract) && outStatus &&
           contract->ops->coverage_status &&
           contract->ops->coverage_status(contract, outStatus);
}

bool SurfaceContractCoverageUpdate(const SurfaceContract *contract,
                                   SurfaceAddress focus, double radius,
                                   unsigned submissionBudget,
                                   SurfaceCoverageStatus *outStatus)
{
    return ContractIsUsable(contract) && outStatus &&
           contract->ops->coverage_update &&
           contract->ops->coverage_update(contract, focus, radius,
                                          submissionBudget, outStatus);
}

bool SurfaceContractMatchesTopology(const SurfaceContract *contract,
                                    SurfaceTopologyVersion topology,
                                    uint32_t bodyId)
{
    return ContractIsUsable(contract) && contract->bodyId == bodyId &&
           contract->topology.kind == topology.kind &&
           contract->topology.version == topology.version;
}
