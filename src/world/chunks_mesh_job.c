#include "world/chunks_internal.h"

void BuildMeshJobPayload(MeshJob *job)
{
    if (!job) return;
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    bool exact = job->lod == CHUNK_LOD_EXACT;
    if (exact) {
        job->hasMesh = BuildChunkSurfaceSolidMeshData(
            job->blocks, job->sectionY * SURFACE_SECTION_HEIGHT,
            job->cx, job->cz, job->floraStructures,
            job->floraStructureCount, faces,
            job->nearbyIndices, job->nearbyCount,
            job->spherical ? GREEDY_MESH_SPHERICAL_MAX_SPAN
                           : GREEDY_MESH_MAX_SPAN,
            &job->boundary, &job->mesh);
        job->hasWaterMesh = BuildChunkSurfaceWaterMeshDataWithSnapshot(
            job->blocks, (const unsigned char *)job->waterVolumes,
            job->sectionY * SURFACE_SECTION_HEIGHT,
            job->cx, job->cz, job->floraStructures,
            job->floraStructureCount, faces,
            job->nearbyIndices, job->nearbyCount,
            &job->boundary, &job->waterMesh);
        job->hasFloraMesh = BuildChunkFloraMeshDataFromSnapshot(
            job->blocks, job->sectionY * SURFACE_SECTION_HEIGHT,
            job->cx, job->cz, job->floraStructures,
            job->floraStructureCount, faces,
            job->nearbyIndices, job->nearbyCount,
            &job->boundary, &job->floraMesh,
            &job->floraInstances, &job->floraInstanceCount);
    } else {
        job->hasMesh = BuildChunkLodHeightfieldMeshData(
            job->blocks, job->cx, job->cz, job->lod,
            &job->boundary, &job->mesh);
        job->hasWaterMesh = BuildChunkLodWaterHeightfieldMeshData(
            job->blocks, job->waterVolumes, job->cx, job->cz,
            job->lod, &job->boundary, &job->waterMesh);
    }

    if (job->spherical) {
        if (job->hasMesh) {
            CurveChunkMeshData(
                &job->mesh, job->cx, job->cz, job->sectionY,
                job->surfaceAddress.bodyId,
                job->surfaceMapOriginX, job->surfaceMapOriginZ);
        }
        if (job->hasWaterMesh) {
            CurveChunkMeshData(
                &job->waterMesh, job->cx, job->cz, job->sectionY,
                job->surfaceAddress.bodyId,
                job->surfaceMapOriginX, job->surfaceMapOriginZ);
        }
        if (job->hasFloraMesh) {
            CurveChunkMeshData(
                &job->floraMesh, job->cx, job->cz, job->sectionY,
                job->surfaceAddress.bodyId,
                job->surfaceMapOriginX, job->surfaceMapOriginZ);
        }
        if (exact) {
            CurveChunkFloraInstances(
                job->floraInstances, job->floraInstanceCount,
                job->cx, job->cz, job->sectionY,
                job->surfaceAddress.bodyId,
                job->surfaceMapOriginX, job->surfaceMapOriginZ);
        }
    } else {
        if (job->hasMesh) {
            LocalizeChunkMeshData(&job->mesh, job->cx, job->cz);
        }
        if (job->hasWaterMesh) {
            LocalizeChunkMeshData(&job->waterMesh, job->cx, job->cz);
        }
        if (job->hasFloraMesh) {
            LocalizeChunkMeshData(&job->floraMesh, job->cx, job->cz);
        }
        if (exact) {
            LocalizeChunkFloraInstances(
                job->floraInstances, job->floraInstanceCount,
                job->cx, job->cz);
        }
    }
}

void RebuildChunkSectionMeshSync(Chunk *chunk, ChunkSection *section)
{
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    ChunkLodLevel lod = ChunkLodSanitize(chunk->targetLod);
    int nearbyTorchIndices[MAX_TORCH_LIGHTS] = { 0 };
    int nearbyTorchCount = lod == CHUNK_LOD_EXACT
        ? CollectNearbyTorchLights(
              chunk->cx * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
              chunk->cx * CHUNK_SIZE + CHUNK_SIZE - 1 +
                  (int)TORCH_LIGHT_RADIUS,
              chunk->cz * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
              chunk->cz * CHUNK_SIZE + CHUNK_SIZE - 1 +
                  (int)TORCH_LIGHT_RADIUS,
              nearbyTorchIndices)
        : 0;
    Mesh solidMesh = { 0 };
    Mesh waterMesh = { 0 };
    Mesh floraMesh = { 0 };
    FloraVisualInstance *floraInstances = NULL;
    int floraInstanceCount = 0;
    SurfaceBoundarySnapshot boundary = { 0 };
    CaptureSurfaceBoundary(
        &boundary, chunk->cx, chunk->cz, section->sectionY);
    bool hasSolid = lod == CHUNK_LOD_EXACT
        ? BuildChunkSurfaceSolidMeshData(
              section->blocks, section->sectionY * SURFACE_SECTION_HEIGHT,
              chunk->cx, chunk->cz,
              chunk->floraStructures, chunk->floraStructureCount,
              faces, nearbyTorchIndices, nearbyTorchCount,
              chunk->spherical ? GREEDY_MESH_SPHERICAL_MAX_SPAN
                               : GREEDY_MESH_MAX_SPAN,
              &boundary, &solidMesh)
        : BuildChunkLodHeightfieldMeshData(
              section->blocks, chunk->cx, chunk->cz, lod,
              &boundary, &solidMesh);
    bool hasWater = lod == CHUNK_LOD_EXACT
        ? BuildChunkSurfaceWaterMeshDataWithSnapshot(
              section->blocks, section->waterVolumes,
              section->sectionY * SURFACE_SECTION_HEIGHT,
              chunk->cx, chunk->cz,
              chunk->floraStructures, chunk->floraStructureCount,
              faces, nearbyTorchIndices, nearbyTorchCount,
              &boundary, &waterMesh)
        : BuildChunkLodWaterHeightfieldMeshData(
              section->blocks,
              (const unsigned char (*)[SURFACE_SECTION_HEIGHT][CHUNK_SIZE])
                  section->waterVolumes,
              chunk->cx, chunk->cz, lod, &boundary, &waterMesh);
    bool hasFlora = lod == CHUNK_LOD_EXACT &&
        BuildChunkFloraMeshDataFromSnapshot(
            section->blocks, section->sectionY * SURFACE_SECTION_HEIGHT,
            chunk->cx, chunk->cz,
            chunk->floraStructures, chunk->floraStructureCount,
            faces, nearbyTorchIndices, nearbyTorchCount, &boundary,
            &floraMesh, &floraInstances, &floraInstanceCount);

    if (chunk->spherical) {
        int mapOriginX = WorldSurfaceMapOriginX();
        int mapOriginZ = WorldSurfaceMapOriginZ();
        if (hasSolid) {
            CurveChunkMeshData(
                &solidMesh, chunk->cx, chunk->cz, section->sectionY,
                chunk->surfaceAddress.bodyId, mapOriginX, mapOriginZ);
        }
        if (hasWater) {
            CurveChunkMeshData(
                &waterMesh, chunk->cx, chunk->cz, section->sectionY,
                chunk->surfaceAddress.bodyId, mapOriginX, mapOriginZ);
        }
        if (hasFlora) {
            CurveChunkMeshData(
                &floraMesh, chunk->cx, chunk->cz, section->sectionY,
                chunk->surfaceAddress.bodyId, mapOriginX, mapOriginZ);
        }
        CurveChunkFloraInstances(
            floraInstances, floraInstanceCount, chunk->cx, chunk->cz,
            section->sectionY, chunk->surfaceAddress.bodyId,
            mapOriginX, mapOriginZ);
    } else {
        if (hasSolid) LocalizeChunkMeshData(&solidMesh, chunk->cx, chunk->cz);
        if (hasWater) LocalizeChunkMeshData(&waterMesh, chunk->cx, chunk->cz);
        if (hasFlora) LocalizeChunkMeshData(&floraMesh, chunk->cx, chunk->cz);
        LocalizeChunkFloraInstances(floraInstances, floraInstanceCount,
                                    chunk->cx, chunk->cz);
    }

    if (lod == CHUNK_LOD_EXACT) {
        ReplaceChunkModel(&section->model, &section->hasModel,
                          &solidMesh, hasSolid, false);
        section->exactModelReady = true;
        section->exactModelStamp = section->dirtyStamp;
    } else {
        ReplaceChunkModel(&section->lodModel, &section->hasLodModel,
                          &solidMesh, hasSolid, false);
        section->lodModelReady = true;
        section->lodModelStamp = section->dirtyStamp;
        section->lodModelLevel = lod;
    }
    if (lod == CHUNK_LOD_EXACT) {
        ReplaceChunkModel(&section->waterModel, &section->hasWaterModel,
                          &waterMesh, hasWater, false);
        ReplaceChunkModel(&section->floraModel, &section->hasFloraModel,
                          &floraMesh, hasFlora, true);
        InitializeFloraTargets(section, floraInstances, floraInstanceCount);
    } else {
        ReplaceChunkModel(&section->lodWaterModel,
                          &section->hasLodWaterModel,
                          &waterMesh, hasWater, false);
    }
    free(floraInstances);
    section->floraVisualScale = 1.0f;
    section->dirty = false;
    section->dirtySinceMs = 0.0;
    ChunkRefreshActiveLod(chunk);
}
