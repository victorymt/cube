#include "surface_contract_fixture.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>

static SurfaceChunkHandle AssertChunkIdentity(
    const SurfaceContract *contract, SurfaceAddress address)
{
    SurfaceChunkHandle chunk;
    SurfaceChunkHandle chunkBack;
    assert(SurfaceContractChunkFromAddress(contract, address, &chunk));
    assert(SurfaceContractChunkIsValid(contract, chunk));
    SurfaceAddress anchor;
    assert(SurfaceContractChunkAnchorAddress(contract, chunk, &anchor));
    assert(SurfaceContractChunkFromAddress(contract, anchor, &chunkBack));
    assert(SurfaceContractChunkEqual(contract, chunk, chunkBack));
    assert(SurfaceContractChunkHash(contract, chunk) ==
           SurfaceContractChunkHash(contract, chunkBack));
    return chunk;
}

static void AssertNeighbourRoundTrip(const SurfaceContract *contract,
                                     SurfaceAddress address,
                                     SurfaceDirection direction)
{
    SurfaceAddress next;
    SurfaceDirection back;
    assert(SurfaceContractNeighbor(contract, address, direction, &next, &back));
    SurfaceAddress returned;
    assert(SurfaceContractNeighbor(contract, next, back, &returned, NULL));
    assert(SurfaceContractAddressEqual(address, returned));
}

static void AssertSample(const SurfaceContract *contract,
                         SurfaceAddress address)
{
    SurfaceContractSample sample;
    assert(SurfaceContractSampleAt(contract, address, &sample));
    assert(isfinite(sample.elevation));
    assert(isfinite(sample.seaLevel));
    assert(isfinite(sample.waterSurface));
    assert(sample.waterDepth >= 0);
    assert(sample.biome >= BIOME_PLAINS && sample.biome <= BIOME_SWAMP);

    int seabedY = 0;
    int waterDepth = -1;
    assert(SurfaceContractWaterSampleAt(contract, address, &seabedY,
                                        &waterDepth));
    assert(waterDepth >= 0);
    assert(waterDepth == sample.waterDepth);
}

void SurfaceContractFixtureAssert(const SurfaceContractFixture *fixture)
{
    assert(fixture && fixture->contract && fixture->reload);
    const SurfaceContract *contract = fixture->contract;
    assert(SurfaceContractIsValid(contract));
    assert(SurfaceContractAddressIsValid(fixture->primary));

    assert(SurfaceContractCellIsKnown(contract, fixture->primary));
    WorldCell cell;
    assert(SurfaceContractCellRead(contract, fixture->primary, &cell));
    assert(WorldCellIsValid(cell));

    AssertSample(contract, fixture->primary);

    WorldCell solid = { BLOCK_STONE, 0u };
    assert(SurfaceContractCellWrite(contract, fixture->primary, solid));
    assert(SurfaceContractCellRead(contract, fixture->primary, &cell));
    assert(WorldCellEqual(cell, solid));

    WorldCell water = { BLOCK_AIR, 127u };
    assert(SurfaceContractCellWrite(contract, fixture->primary, water));
    assert(SurfaceContractCellRead(contract, fixture->primary, &cell));
    assert(WorldCellEqual(cell, water));
    assert(!SurfaceContractCellWrite(
        contract, fixture->primary, (WorldCell){ BLOCK_BRICK, 255u }));
    assert(SurfaceContractCellRead(contract, fixture->primary, &cell));
    assert(WorldCellEqual(cell, water));

    SurfaceChunkHandle primaryChunk =
        AssertChunkIdentity(contract, fixture->primary);
    SurfaceSectionHandle section;
    assert(SurfaceContractSectionFromAddress(
        contract, fixture->primary, &section));
    assert(SurfaceContractSectionIsValid(contract, section));

    AssertNeighbourRoundTrip(contract, fixture->primary,
                             SURFACE_DIRECTION_U_PLUS);
    SurfaceAddress horizontal;
    SurfaceDirection horizontalBack;
    assert(SurfaceContractNeighbor(contract, fixture->primary,
                                   SURFACE_DIRECTION_U_PLUS, &horizontal,
                                   &horizontalBack));
    assert(horizontalBack == SURFACE_DIRECTION_U_MINUS);
    SurfaceRegionHandle region =
        SurfaceContractRegionForAddress(contract, fixture->primary);
    assert(region.opaque[0] != 0u || region.opaque[1] != 0u);
    assert(SurfaceContractSameRegion(contract, fixture->primary, horizontal));

    if (fixture->hasTwin) {
        assert(SurfaceContractAddressIsValid(fixture->twin));
        assert(!SurfaceContractAddressEqual(fixture->primary, fixture->twin));
        SurfaceChunkHandle twinChunk =
            AssertChunkIdentity(contract, fixture->twin);
        assert(!SurfaceContractChunkEqual(contract, primaryChunk, twinChunk));
    }

    if (fixture->hasCrossEdge) {
        assert(SurfaceContractAddressIsValid(fixture->crossEdge));
        SurfaceAddress across;
        SurfaceDirection back;
        assert(SurfaceContractNeighbor(contract, fixture->crossEdge,
                                       fixture->crossDirection, &across,
                                       &back));
        assert((across.face != fixture->crossEdge.face) ==
               fixture->crossEdgeChangesFace);
        SurfaceAddress returned;
        assert(SurfaceContractNeighbor(contract, across, back, &returned,
                                       NULL));
        assert(SurfaceContractAddressEqual(fixture->crossEdge, returned));
        if (fixture->crossEdgeChangesFace) {
            assert(!SurfaceContractSameRegion(contract, fixture->crossEdge,
                                              across));
            SurfaceChunkHandle edgeChunk = AssertChunkIdentity(
                contract, fixture->crossEdge);
            SurfaceChunkHandle acrossChunk;
            SurfaceDirection chunkBack;
            assert(SurfaceContractChunkNeighbor(
                contract, edgeChunk, fixture->crossDirection, &acrossChunk,
                &chunkBack));
            assert(!SurfaceContractChunkEqual(
                contract, edgeChunk, acrossChunk));
            SurfaceChunkHandle edgeChunkBack;
            assert(SurfaceContractChunkNeighbor(
                contract, acrossChunk, chunkBack, &edgeChunkBack, NULL));
            assert(SurfaceContractChunkEqual(
                contract, edgeChunk, edgeChunkBack));
        }
    }

    fixture->reload(fixture->reloadContext);
    assert(SurfaceContractIsValid(contract));
    assert(SurfaceContractChunkIsValid(contract, primaryChunk));
    assert(SurfaceContractChunkEqual(
        contract, primaryChunk, AssertChunkIdentity(contract, fixture->primary)));
    SurfaceSectionHandle sectionAfter;
    assert(SurfaceContractSectionFromAddress(
        contract, fixture->primary, &sectionAfter));
    assert(sectionAfter.sectionY == section.sectionY);
    assert(SurfaceContractChunkEqual(
        contract, section.chunk, sectionAfter.chunk));
    assert(SurfaceContractSectionIsValid(contract, sectionAfter));
    assert(SurfaceContractCellIsKnown(contract, fixture->primary));
}
