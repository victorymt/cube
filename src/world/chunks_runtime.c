#include "world/chunks_internal.h"
#include "ecology/flora_taxa.h"

MeshJob meshJobs[MAX_MESH_JOBS];
static uint64_t nextMeshQueueSequence = 0u;

void UpdateQueuePeaksLocked(void)
{
    uint64_t generation = 0;
    uint64_t mesh = 0;
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse && !chunkGenJobs[i].done) generation++;
    }
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse && !meshJobs[i].done) mesh++;
    }
    if (generation > streamingStats.generationQueuePeak) {
        streamingStats.generationQueuePeak = generation;
    }
    if (mesh > streamingStats.meshQueuePeak) streamingStats.meshQueuePeak = mesh;
    uint64_t snapshotBytes = 0;
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse) {
            snapshotBytes += sizeof(meshJobs[i].blocks) +
                             sizeof(meshJobs[i].waterVolumes) +
                             sizeof(meshJobs[i].boundary);
        }
    }
    streamingStats.pendingMeshSnapshotBytes = snapshotBytes;
    if (snapshotBytes > streamingStats.pendingMeshSnapshotBytesPeak) {
        streamingStats.pendingMeshSnapshotBytesPeak = snapshotBytes;
    }
}

bool HasPendingMeshJob(void)
{
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse && !meshJobs[i].done) return true;
    }
    return false;
}

MeshJob *NextPendingMeshJob(void)
{
    MeshJob *oldest = NULL;
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        MeshJob *job = &meshJobs[i];
        if (!job->inUse || job->running || job->done) continue;
        if (!oldest || job->queueSequence < oldest->queueSequence) {
            oldest = job;
        }
    }
    return oldest;
}

static bool FindPendingMeshJob(int slotIndex, int sectionY)
{
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse && meshJobs[i].slotIndex == slotIndex &&
            meshJobs[i].sectionY == sectionY) return true;
    }
    return false;
}

static bool ChunkSectionHasFluidRuntimeState(const ChunkSection *section)
{
    return section && (section->waterVolumes || section->fluidQueuedBits ||
                       section->fluidDeferredBits || section->fluidFlow ||
                       section->fluidDirty);
}

static bool ChunkSectionHasInFlightWork(
    int slotIndex, uint32_t chunkGeneration, int sectionY)
{
    bool pending = false;
    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        const ChunkGenJob *job = &chunkGenJobs[i];
        if (job->inUse && job->scope == CHUNK_GEN_SCOPE_SECTION &&
            job->slotIndex == slotIndex &&
            job->chunkGeneration == chunkGeneration &&
            job->sectionY == sectionY) {
            pending = true;
            break;
        }
    }
    if (!pending) pending = FindPendingMeshJob(slotIndex, sectionY);
    pthread_mutex_unlock(&genMutex);
    return pending;
}

static void ChunkForgetTerrainSectionResolved(Chunk *chunk, int sectionY)
{
    if (!chunk) return;
    int index = ResolvedTerrainSectionLowerBound(chunk, sectionY);
    if (index >= chunk->resolvedTerrainSectionCount ||
        chunk->resolvedTerrainSectionYs[index] != sectionY) {
        return;
    }
    if (index + 1 < chunk->resolvedTerrainSectionCount) {
        memmove(&chunk->resolvedTerrainSectionYs[index],
                &chunk->resolvedTerrainSectionYs[index + 1],
                (size_t)(chunk->resolvedTerrainSectionCount - index - 1) *
                    sizeof(*chunk->resolvedTerrainSectionYs));
    }
    chunk->resolvedTerrainSectionCount--;
}

static bool ChunkRemoveTerrainSection(Chunk *chunk, int sectionY)
{
    if (!chunk) return false;
    int index = ChunkSectionLowerBound(chunk, sectionY);
    if (index >= chunk->sectionCount ||
        chunk->sections[index]->sectionY != sectionY) {
        return false;
    }

    ChunkSection *section = chunk->sections[index];
    if (index + 1 < chunk->sectionCount) {
        memmove(&chunk->sections[index], &chunk->sections[index + 1],
                (size_t)(chunk->sectionCount - index - 1) *
                    sizeof(*chunk->sections));
    }
    chunk->sectionCount--;
    ChunkForgetTerrainSectionResolved(chunk, sectionY);
    FreeChunkSectionStorage(section);
    MarkGeneratedSectionAndNeighborsDirty(chunk, sectionY);
    return true;
}

int PruneDistantNegativeTerrainSections(int playerSectionY)
{
    if (!HomeWorldSurfaceIsActive()) return 0;

    int pruned = 0;
    for (int slotIndex = 0; slotIndex < MAX_ACTIVE_CHUNKS; slotIndex++) {
        Chunk *chunk = &chunks[slotIndex];
        if (!chunk->loaded) continue;

        int sectionIndex = 0;
        while (sectionIndex < chunk->sectionCount) {
            ChunkSection *section = chunk->sections[sectionIndex];
            int sectionY = section->sectionY;
            if (sectionY >= 0) break;
            if (!NegativeTerrainSectionOutsideWindow(
                    sectionY, playerSectionY)) {
                sectionIndex++;
                continue;
            }
            if (ChunkSectionHasInFlightWork(
                    slotIndex, chunk->generation, sectionY)) {
                sectionIndex++;
                continue;
            }
            if (ChunkSectionHasFluidRuntimeState(section) &&
                !WorldPrepareChunkSectionUnload(chunk, sectionY)) {
                sectionIndex++;
                continue;
            }
            if (ChunkRemoveTerrainSection(chunk, sectionY)) {
                pruned++;
                continue;
            }
            sectionIndex++;
        }
    }
    return pruned;
}

void FreeMeshData(Mesh *mesh)
{
    free(mesh->vertices);
    free(mesh->texcoords);
    free(mesh->texcoords2);
    free(mesh->normals);
    free(mesh->colors);
    *mesh = (Mesh){ 0 };
}

void LocalizeChunkMeshData(Mesh *mesh, int chunkX, int chunkZ)
{
    if (!mesh || !mesh->vertices || mesh->vertexCount <= 0) return;
    float originX = (float)(chunkX * CHUNK_SIZE);
    float originZ = (float)(chunkZ * CHUNK_SIZE);
    for (int vertex = 0; vertex < mesh->vertexCount; vertex++) {
        mesh->vertices[vertex * 3 + 0] -= originX;
        mesh->vertices[vertex * 3 + 2] -= originZ;
    }
}

void LocalizeChunkFloraInstances(FloraVisualInstance *instances, int count,
                                 int chunkX, int chunkZ)
{
    if (!instances || count <= 0) return;
    float originX = (float)(chunkX * CHUNK_SIZE);
    float originZ = (float)(chunkZ * CHUNK_SIZE);
    for (int index = 0; index < count; index++) {
        instances[index].anchor.x -= originX;
        instances[index].anchor.z -= originZ;
    }
}

static Vector3 CurveSurfacePoint(const SurfaceFrame *anchorFrame,
                                 uint32_t bodyId, float mapX, float localY,
                                 float mapZ, int radialBase)
{
    (void)bodyId;
    SurfaceFrame vertexFrame = SurfaceLocalFrameAtOffset(
        mapX, mapZ, radialBase);
    Vector3 planet = Vector3Add(
        vertexFrame.origin, Vector3Scale(vertexFrame.up, localY));
    return SurfaceFramePlanetToLocal(anchorFrame, planet);
}

void CurveChunkMeshData(Mesh *mesh, int chunkX, int chunkZ, int sectionY,
                        uint32_t bodyId, int mapOriginX, int mapOriginZ)
{
    if (!mesh || !mesh->vertices || mesh->vertexCount <= 0) return;
    (void)bodyId;
    (void)mapOriginX;
    (void)mapOriginZ;
    int radialBase = sectionY * SURFACE_SECTION_HEIGHT;
    float anchorX = (float)(chunkX * CHUNK_SIZE);
    float anchorZ = (float)(chunkZ * CHUNK_SIZE);
    SurfaceFrame anchorFrame = SurfaceLocalFrameAtOffset(
        0.0f, 0.0f, radialBase);
    for (int vertex = 0; vertex < mesh->vertexCount; vertex++) {
        float offsetX = mesh->vertices[vertex * 3 + 0] - anchorX;
        float localY = mesh->vertices[vertex * 3 + 1];
        float offsetZ = mesh->vertices[vertex * 3 + 2] - anchorZ;
        Vector3 curved = CurveSurfacePoint(
            &anchorFrame, bodyId, offsetX, localY, offsetZ, radialBase);
        mesh->vertices[vertex * 3 + 0] = curved.x;
        mesh->vertices[vertex * 3 + 1] = curved.y;
        mesh->vertices[vertex * 3 + 2] = curved.z;

        if (mesh->normals) {
            SurfaceFrame vertexFrame = SurfaceLocalFrameAtOffset(
                offsetX, offsetZ, radialBase);
            Vector3 source = {
                mesh->normals[vertex * 3 + 0],
                mesh->normals[vertex * 3 + 1],
                mesh->normals[vertex * 3 + 2]
            };
            Vector3 planet = Vector3Add(
                Vector3Scale(vertexFrame.east, source.x),
                Vector3Scale(vertexFrame.up, source.y));
            planet = Vector3Add(
                planet, Vector3Scale(vertexFrame.north, source.z));
            Vector3 normal = {
                Vector3DotProduct(planet, anchorFrame.east),
                Vector3DotProduct(planet, anchorFrame.up),
                Vector3DotProduct(planet, anchorFrame.north)
            };
            normal = Vector3Normalize(normal);
            mesh->normals[vertex * 3 + 0] = normal.x;
            mesh->normals[vertex * 3 + 1] = normal.y;
            mesh->normals[vertex * 3 + 2] = normal.z;
        }
    }
}

void CurveChunkFloraInstances(
    FloraVisualInstance *instances, int count, int chunkX, int chunkZ,
    int sectionY, uint32_t bodyId, int mapOriginX, int mapOriginZ)
{
    if (!instances || count <= 0) return;
    (void)bodyId;
    (void)mapOriginX;
    (void)mapOriginZ;
    int radialBase = sectionY * SURFACE_SECTION_HEIGHT;
    float anchorX = (float)(chunkX * CHUNK_SIZE);
    float anchorZ = (float)(chunkZ * CHUNK_SIZE);
    SurfaceFrame anchorFrame = SurfaceLocalFrameAtOffset(
        0.0f, 0.0f, radialBase);
    for (int index = 0; index < count; index++) {
        float offsetX = instances[index].anchor.x - anchorX;
        float offsetZ = instances[index].anchor.z - anchorZ;
        instances[index].anchor = CurveSurfacePoint(
            &anchorFrame, bodyId, offsetX, instances[index].anchor.y, offsetZ,
            radialBase);
    }
}

int CancelDistantNegativeSectionJobs(int playerSectionY)
{
    if (!HomeWorldSurfaceIsActive()) return 0;

    int canceled = 0;
    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        ChunkGenJob *job = &chunkGenJobs[i];
        if (!job->inUse || job->running ||
            job->scope != CHUNK_GEN_SCOPE_SECTION ||
            !NegativeTerrainSectionOutsideWindow(
                job->sectionY, playerSectionY)) {
            continue;
        }
        *job = (ChunkGenJob){ 0 };
        streamingStats.generationCanceled++;
        canceled++;
    }
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        MeshJob *job = &meshJobs[i];
        if (!job->inUse || job->running ||
            !NegativeTerrainSectionOutsideWindow(
                job->sectionY, playerSectionY)) {
            continue;
        }
        FreeMeshData(&job->mesh);
        FreeMeshData(&job->waterMesh);
        FreeMeshData(&job->floraMesh);
        free(job->floraInstances);
        *job = (MeshJob){ 0 };
        streamingStats.meshCanceled++;
        canceled++;
    }
    UpdateQueuePeaksLocked();
    pthread_mutex_unlock(&genMutex);
    return canceled;
}

static void ReplaceChunkModel(Model *model, bool *hasModel,
                              Mesh *mesh, bool hasMesh, bool dynamic)
{
    if (!hasMesh) {
        if (*hasModel) {
            UnloadModel(*model);
            *model = (Model){ 0 };
            *hasModel = false;
        }
        FreeMeshData(mesh);
        return;
    }

    UploadMesh(mesh, dynamic);
    Model replacement = LoadModelFromMesh(*mesh);
    if (replacement.materialCount > 0 && replacement.materials) {
        SetMaterialTexture(&replacement.materials[0], MATERIAL_MAP_DIFFUSE,
                           blockAtlas);
    }
    if (*hasModel) UnloadModel(*model);
    *model = replacement;
    *hasModel = true;
}

static void InitializeFloraTargets(
    ChunkSection *section, const FloraVisualInstance *sourceInstances,
    int sourceInstanceCount)
{
    ClearSectionFloraRuntime(section);
    if (!section || !section->hasFloraModel || section->floraModel.meshCount <= 0) return;

    Mesh *mesh = &section->floraModel.meshes[0];
    if (mesh->vertexCount <= 0 || !mesh->vertices || !mesh->colors) return;
    bool hasSourceInstances = sourceInstances && sourceInstanceCount > 0;
    int count = hasSourceInstances ? sourceInstanceCount :
                (mesh->vertexCount + 11) / 12;
    section->floraTargetScales = malloc((size_t)count * sizeof(float));
    section->floraTargetWind = malloc((size_t)count * sizeof(float));
    section->floraTargetWindAngle = malloc((size_t)count * sizeof(float));
    section->floraTargetPresence = malloc((size_t)count * sizeof(float));
    section->floraBaseVertices = malloc(
        (size_t)mesh->vertexCount * 3u * sizeof(float));
    section->floraBaseColors = malloc((size_t)mesh->vertexCount * 4u);
    section->floraVisualInstances = malloc(
        (size_t)count * sizeof(FloraVisualInstance));
    if (!section->floraTargetScales || !section->floraTargetWind ||
        !section->floraTargetWindAngle || !section->floraTargetPresence ||
        !section->floraBaseVertices ||
        !section->floraBaseColors || !section->floraVisualInstances) {
        ClearSectionFloraRuntime(section);
        return;
    }
    memcpy(section->floraBaseVertices, mesh->vertices,
           (size_t)mesh->vertexCount * 3u * sizeof(float));
    memcpy(section->floraBaseColors, mesh->colors,
           (size_t)mesh->vertexCount * 4u);
    if (hasSourceInstances) {
        memcpy(section->floraVisualInstances, sourceInstances,
               (size_t)count * sizeof(FloraVisualInstance));
    }
    for (int index = 0; index < count; index++) {
        if (!hasSourceInstances) {
            int firstVertex = index * 12;
            int vertexCount = mesh->vertexCount - firstVertex;
            if (vertexCount > 12) vertexCount = 12;
            float groundY = INFINITY;
            for (int vertex = firstVertex;
                 vertex < firstVertex + vertexCount; vertex++) {
                groundY = fminf(groundY, mesh->vertices[vertex * 3 + 1]);
            }
            float firstX = mesh->vertices[firstVertex * 3];
            float firstZ = mesh->vertices[firstVertex * 3 + 2];
            section->floraVisualInstances[index] = (FloraVisualInstance){
                .firstVertex = firstVertex,
                .vertexCount = vertexCount,
                .taxonId = FLORA_TAXON_COUNT,
                .anchor = {
                    floorf(firstX) + 0.5f,
                    groundY,
                    floorf(firstZ) + 0.5f
                },
                .height = 0.4f,
                .windResponse = 1.0f
            };
        }
        section->floraTargetScales[index] = 1.0f;
        section->floraTargetWind[index] = 0.0f;
        section->floraTargetWindAngle[index] = 0.0f;
        section->floraTargetPresence[index] = 1.0f;
    }
    section->floraTargetScaleCount = count;
}

static bool UploadMeshJob(MeshJob *job)
{
    Chunk *chunk = NULL;
    ChunkSection *section = NULL;
    bool targetValid = false;
    bool snapshotCurrent = false;
    if (job && job->slotIndex >= 0 && job->slotIndex < MAX_ACTIVE_CHUNKS) {
        chunk = &chunks[job->slotIndex];
        section = ChunkGetSection(chunk, job->sectionY, false);
        targetValid = chunk->loaded && chunk->cx == job->cx &&
                      chunk->cz == job->cz &&
                      chunk->generation == job->chunkGeneration &&
                      chunk->spherical == job->spherical &&
                      (!job->spherical || SurfaceAddressEqual(
                          chunk->surfaceAddress, job->surfaceAddress)) &&
                      section != NULL;
        snapshotCurrent = targetValid &&
                          section->dirtyStamp == job->sectionStamp;
    }

    if (job && snapshotCurrent) {
        ReplaceChunkModel(&section->model, &section->hasModel,
                          &job->mesh, job->hasMesh, false);
        ReplaceChunkModel(&section->waterModel, &section->hasWaterModel,
                          &job->waterMesh, job->hasWaterMesh, false);
        ReplaceChunkModel(&section->floraModel, &section->hasFloraModel,
                          &job->floraMesh, job->hasFloraMesh, true);
        InitializeFloraTargets(section, job->floraInstances,
                               job->floraInstanceCount);
        section->floraVisualScale = 1.0f;
    } else if (job) {
        FreeMeshData(&job->mesh);
        FreeMeshData(&job->waterMesh);
        FreeMeshData(&job->floraMesh);
    }

    if (job) {
        free(job->floraInstances);
        job->floraInstances = NULL;
        job->floraInstanceCount = 0;
    }

    pthread_mutex_lock(&genMutex);
    if (job) job->inUse = false;
    bool pending = job && snapshotCurrent && FindPendingMeshJob(
        job->slotIndex, job->sectionY);
    if (job && !snapshotCurrent) streamingStats.meshCanceled++;
    pthread_mutex_unlock(&genMutex);
    if (snapshotCurrent && !pending) {
        section->dirty = false;
        section->dirtySinceMs = 0.0;
    }
    return snapshotCurrent;
}

static void PrepareMeshJob(MeshJob *job, const Chunk *chunk,
                           const ChunkSection *section)
{
    memcpy(job->blocks, section->blocks, sizeof(job->blocks));
    if (section->waterVolumes) {
        memcpy(job->waterVolumes, section->waterVolumes,
               sizeof(job->waterVolumes));
    } else {
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int y = 0; y < SURFACE_SECTION_HEIGHT; y++) {
                for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                    job->waterVolumes[lx][y][lz] =
                        section->blocks[lx][y][lz] == BLOCK_WATER
                            ? (unsigned char)WATER_VOLUME_CAPACITY : 0u;
                }
            }
        }
    }
    CaptureSurfaceBoundary(
        &job->boundary, chunk->cx, chunk->cz, section->sectionY);
    job->floraStructureCount = chunk->floraStructureCount;
    if (job->floraStructureCount < 0) job->floraStructureCount = 0;
    if (job->floraStructureCount > MAX_CHUNK_FLORA_STRUCTURES) {
        job->floraStructureCount = MAX_CHUNK_FLORA_STRUCTURES;
    }
    memcpy(job->floraStructures, chunk->floraStructures,
           (size_t)job->floraStructureCount * sizeof(FloraStructureInstance));
    job->nearbyCount = CollectNearbyTorchLights(
        chunk->cx * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cx * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        job->nearbyIndices);
    job->inUse = true;
    job->done = false;
    job->slotIndex = (int)(chunk - chunks);
    job->cx = chunk->cx;
    job->cz = chunk->cz;
    job->sectionY = section->sectionY;
    job->sectionStamp = section->dirtyStamp;
    job->chunkGeneration = chunk->generation;
    job->queueSequence = ++nextMeshQueueSequence;
    job->submittedAtMs = ChunkNowMs();
    job->startedAtMs = 0.0;
    job->completedAtMs = 0.0;
    job->spherical = chunk->spherical;
    job->surfaceAddress = chunk->surfaceAddress;
    job->surfaceMapOriginX = WorldSurfaceMapOriginX();
    job->surfaceMapOriginZ = WorldSurfaceMapOriginZ();
    job->mesh = (Mesh){ 0 };
    job->waterMesh = (Mesh){ 0 };
    job->floraMesh = (Mesh){ 0 };
    free(job->floraInstances);
    job->floraInstances = NULL;
    job->floraInstanceCount = 0;
    job->hasMesh = false;
    job->hasWaterMesh = false;
    job->hasFloraMesh = false;
}

static bool SubmitMeshJobs(Chunk *chunk, ChunkSection *section)
{
    if (!chunk || !section || genThread == 0) return false;

    pthread_mutex_lock(&genMutex);
    MeshJob *job = NULL;
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (!meshJobs[i].inUse) {
            job = &meshJobs[i];
            break;
        }
    }
    if (!job) {
        pthread_mutex_unlock(&genMutex);
        return false;
    }

    PrepareMeshJob(job, chunk, section);
    streamingStats.meshSubmitted++;
    streamingStats.meshSnapshotBytes += sizeof(job->blocks) +
                                        sizeof(job->waterVolumes) +
                                        sizeof(job->boundary);
    UpdateQueuePeaksLocked();
    pthread_cond_signal(&genCond);
    pthread_mutex_unlock(&genMutex);
    return true;
}

void ProcessFinishedMeshJobs(double uploadBudgetMs)
{
    int uploaded = 0;
    double startedMs = ChunkNowMs();
    if (!isfinite(uploadBudgetMs) || uploadBudgetMs < 0.0) {
        uploadBudgetMs = 0.0;
    }
    while (uploaded < MAX_MESH_REBUILDS_PER_FRAME) {
        pthread_mutex_lock(&genMutex);
        MeshJob *job = NULL;
        for (int index = 0; index < MAX_MESH_JOBS; index++) {
            MeshJob *candidate = &meshJobs[index];
            if (!candidate->inUse || !candidate->done) continue;
            if (!job || candidate->queueSequence < job->queueSequence) {
                job = candidate;
            }
        }
        pthread_mutex_unlock(&genMutex);
        if (!job) break;
        if (uploaded > 0 && ChunkNowMs() - startedMs >= uploadBudgetMs) {
            pthread_mutex_lock(&genMutex);
            streamingStats.uploadBudgetDeferrals++;
            pthread_mutex_unlock(&genMutex);
            break;
        }
        double uploadStartedMs = ChunkNowMs();
        bool uploadedJob = UploadMeshJob(job);
        double uploadElapsedMs = ChunkNowMs() - uploadStartedMs;
        pthread_mutex_lock(&genMutex);
        if (uploadedJob) streamingStats.uploadedMeshes++;
        streamingStats.uploadCpuMs += uploadElapsedMs;
        if (uploadElapsedMs > streamingStats.maxUploadCpuMs) {
            streamingStats.maxUploadCpuMs = uploadElapsedMs;
        }
        pthread_mutex_unlock(&genMutex);
        uploaded++;
    }
}

void RebuildChunkSectionMeshSync(Chunk *chunk, ChunkSection *section)
{
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };

    int nearbyTorchIndices[MAX_TORCH_LIGHTS];
    int nearbyTorchCount = CollectNearbyTorchLights(
        chunk->cx * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cx * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        nearbyTorchIndices);

    Mesh solidMesh = { 0 };
    Mesh waterMesh = { 0 };
    Mesh floraMesh = { 0 };
    FloraVisualInstance *floraInstances = NULL;
    int floraInstanceCount = 0;
    SurfaceBoundarySnapshot boundary = { 0 };
    CaptureSurfaceBoundary(
        &boundary, chunk->cx, chunk->cz, section->sectionY);
    bool hasSolid = BuildChunkSurfaceSolidMeshData(
        section->blocks, section->sectionY * SURFACE_SECTION_HEIGHT,
        chunk->cx, chunk->cz,
        chunk->floraStructures, chunk->floraStructureCount,
        faces, nearbyTorchIndices, nearbyTorchCount, &boundary, &solidMesh);
    bool hasWater = BuildChunkSurfaceWaterMeshDataWithSnapshot(
        section->blocks, section->waterVolumes,
        section->sectionY * SURFACE_SECTION_HEIGHT,
        chunk->cx, chunk->cz,
        chunk->floraStructures, chunk->floraStructureCount,
        faces, nearbyTorchIndices, nearbyTorchCount, &boundary, &waterMesh);
    bool hasFlora = BuildChunkFloraMeshDataFromSnapshot(
        section->blocks, section->sectionY * SURFACE_SECTION_HEIGHT,
        chunk->cx, chunk->cz,
        chunk->floraStructures, chunk->floraStructureCount,
        faces, nearbyTorchIndices, nearbyTorchCount, &boundary, &floraMesh,
        &floraInstances, &floraInstanceCount);

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

    ReplaceChunkModel(&section->model, &section->hasModel,
                      &solidMesh, hasSolid, false);
    ReplaceChunkModel(&section->waterModel, &section->hasWaterModel,
                      &waterMesh, hasWater, false);
    ReplaceChunkModel(&section->floraModel, &section->hasFloraModel,
                      &floraMesh, hasFlora, true);
    InitializeFloraTargets(section, floraInstances, floraInstanceCount);
    free(floraInstances);
    section->floraVisualScale = 1.0f;
    section->dirty = false;
    section->dirtySinceMs = 0.0;
}

void RebuildDirtyChunkMeshes(Vector3 focusPosition)
{
    int submitted = 0;
    int focusCx = 0;
    int focusCz = 0;
    int localX = 0;
    int localZ = 0;
    WorldToChunkLocal((int)floorf(focusPosition.x),
                      (int)floorf(focusPosition.z),
                      &focusCx, &focusCz, &localX, &localZ);

    int selectedChunks[MAX_MESH_SUBMITS_PER_FRAME] = { 0 };
    int selectedSectionYs[MAX_MESH_SUBMITS_PER_FRAME] = { 0 };
    int focusSectionY = FloorDivInt((int)floorf(focusPosition.y),
                                    SURFACE_SECTION_HEIGHT);
    while (submitted < MAX_MESH_SUBMITS_PER_FRAME) {
        int best = -1;
        int bestSectionIndex = -1;
        int bestSectionY = -1;
        int bestHorizontalDistance = 0;
        int bestVerticalDistance = 0;
        for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
            if (!chunks[i].loaded) continue;
            int dx = abs(chunks[i].cx - focusCx);
            int dz = abs(chunks[i].cz - focusCz);
            int distance = dx > dz ? dx : dz;
            for (int sectionIndex = 0;
                 sectionIndex < chunks[i].sectionCount; sectionIndex++) {
                ChunkSection *section = chunks[i].sections[sectionIndex];
                int sectionY = section->sectionY;
                bool alreadySelected = false;
                for (int selected = 0; selected < submitted; selected++) {
                    if (selectedChunks[selected] == i &&
                        selectedSectionYs[selected] == sectionY) {
                        alreadySelected = true;
                        break;
                    }
                }
                if (alreadySelected || !section->dirty ||
                    FindPendingMeshJob(i, sectionY)) continue;
                int verticalDistance = abs(sectionY - focusSectionY);
                if (best < 0 || distance < bestHorizontalDistance ||
                    (distance == bestHorizontalDistance &&
                     verticalDistance < bestVerticalDistance)) {
                    best = i;
                    bestSectionIndex = sectionIndex;
                    bestSectionY = sectionY;
                    bestHorizontalDistance = distance;
                    bestVerticalDistance = verticalDistance;
                }
            }
        }
        if (best < 0) break;
        selectedChunks[submitted] = best;
        selectedSectionYs[submitted] = bestSectionY;
        ChunkSection *section = chunks[best].sections[bestSectionIndex];

        if (genThread == 0) {
            double startedMs = ChunkNowMs();
            RebuildChunkSectionMeshSync(&chunks[best], section);
            double elapsedMs = ChunkNowMs() - startedMs;
            pthread_mutex_lock(&genMutex);
            streamingStats.syncRebuilds++;
            streamingStats.meshCpuMs += elapsedMs;
            pthread_mutex_unlock(&genMutex);
            submitted++;
            continue;
        }

        if (!SubmitMeshJobs(&chunks[best], section)) break;
        submitted++;
    }
}

bool ChunkWithinDrawDistance(const Chunk *chunk, Vector3 cameraPosition, int effectiveRenderDistance)
{
    int cameraX = (int)floorf(cameraPosition.x);
    int cameraZ = (int)floorf(cameraPosition.z);
    int cameraCx = 0;
    int cameraCz = 0;
    int cameraLx = 0;
    int cameraLz = 0;
    WorldToChunkLocal(cameraX, cameraZ, &cameraCx, &cameraCz, &cameraLx, &cameraLz);

    return abs(chunk->cx - cameraCx) <= effectiveRenderDistance &&
           abs(chunk->cz - cameraCz) <= effectiveRenderDistance;
}

static bool SphereInFrustumWithAspect(const Camera3D *camera, Vector3 center,
                                      float radius, float aspect)
{
    if (!camera || !isfinite(radius) || radius < 0.0f ||
        !isfinite(aspect) || aspect <= 0.0f) {
        return false;
    }
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera->up));
    Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
    Vector3 toCenter = Vector3Subtract(center, camera->position);

    float depth = Vector3DotProduct(toCenter, forward);
    if (depth + radius < CAMERA_NEAR_CULL_DISTANCE) return false;

    float visibleDepth = fmaxf(depth, CAMERA_NEAR_CULL_DISTANCE);
    float verticalTan = tanf(camera->fovy * DEG2RAD * 0.5f);
    float horizontalTan = verticalTan * aspect;
    float horizontalOffset = fabsf(Vector3DotProduct(toCenter, right));
    float verticalOffset = fabsf(Vector3DotProduct(toCenter, up));
    // The side-plane equations are not normalized, so scale the sphere
    // radius by each plane normal's length before comparing distances.
    float horizontalRadius = radius * sqrtf(1.0f + horizontalTan * horizontalTan);
    float verticalRadius = radius * sqrtf(1.0f + verticalTan * verticalTan);

    return horizontalOffset <= visibleDepth * horizontalTan + horizontalRadius &&
           verticalOffset <= visibleDepth * verticalTan + verticalRadius;
}

bool SphereInFrustum(const Camera3D *camera, Vector3 center, float radius)
{
    int renderWidth = GetRenderWidth();
    int renderHeight = GetRenderHeight();
    if (renderWidth <= 0 || renderHeight <= 0) {
        renderWidth = GetScreenWidth();
        renderHeight = GetScreenHeight();
    }
    float aspect = renderWidth > 0 && renderHeight > 0
        ? (float)renderWidth / (float)renderHeight : 1.0f;
    return SphereInFrustumWithAspect(camera, center, radius, aspect);
}

bool ChunkIntersectsCameraView(const Chunk *chunk, const Camera3D *camera)
{
    if (!chunk || !camera) return false;
    for (int index = 0; index < chunk->sectionCount; index++) {
        if (ChunkSectionIntersectsCameraView(
                chunk, chunk->sections[index], camera)) return true;
    }
    return false;
}

bool ChunkSectionIntersectsCameraView(const Chunk *chunk,
                                      const ChunkSection *section,
                                      const Camera3D *camera)
{
    if (!chunk || !section || !camera) return false;
    float half = (float)SURFACE_SECTION_HEIGHT * 0.5f;
    Vector3 center = {
        (float)(chunk->cx * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f,
        (float)(section->sectionY * SURFACE_SECTION_HEIGHT) + half,
        (float)(chunk->cz * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f
    };
    return SphereInFrustum(camera, center, sqrtf(half * half * 3.0f));
}


bool ChunksStartGenThread(void)
{
    if (genThread != 0) return true;
    pthread_mutex_lock(&genMutex);
    genShutdown = false;
    genWorkerActive = false;
    pthread_mutex_unlock(&genMutex);
    return pthread_create(&genThread, NULL, ChunkGenWorker, NULL) == 0;
}

void ChunksShutdownGenThread(void)
{
    DrainChunkGen();
    if (genThread != 0) {
        pthread_mutex_lock(&genMutex);
        genShutdown = true;
        pthread_cond_broadcast(&genCond);
        pthread_mutex_unlock(&genMutex);
        pthread_join(genThread, NULL);
        genThread = 0;
        genWorkerActive = false;
    }
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse) {
            FreeMeshData(&meshJobs[i].mesh);
            FreeMeshData(&meshJobs[i].waterMesh);
            FreeMeshData(&meshJobs[i].floraMesh);
            free(meshJobs[i].floraInstances);
            meshJobs[i].floraInstances = NULL;
            meshJobs[i].floraInstanceCount = 0;
            meshJobs[i].inUse = false;
        }
    }
}

int GetActiveChunkCount(void)
{
    int count = 0;
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (chunks[i].loaded) count++;
    }
    return count;
}

int GetPendingGenJobCount(void)
{
    int count = 0;
    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse && !chunkGenJobs[i].done) count++;
    }
    pthread_mutex_unlock(&genMutex);
    return count;
}

int GetPendingMeshJobCount(void)
{
    int count = 0;
    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse && !meshJobs[i].done) count++;
    }
    pthread_mutex_unlock(&genMutex);
    return count;
}

void ChunksResetStreamingStats(void)
{
    pthread_mutex_lock(&genMutex);
    streamingStats = (ChunkStreamingStats){ 0 };
    UpdateQueuePeaksLocked();
    pthread_mutex_unlock(&genMutex);
}

ChunkStreamingStats ChunksGetStreamingStats(void)
{
    pthread_mutex_lock(&genMutex);
    UpdateQueuePeaksLocked();
    ChunkStreamingStats result = streamingStats;
    pthread_mutex_unlock(&genMutex);
    return result;
}

static int PipelineModelVertexCount(const Model *model, bool present)
{
    if (!present || !model || !model->meshes || model->meshCount <= 0) {
        return 0;
    }
    int vertices = 0;
    for (int index = 0; index < model->meshCount; index++) {
        vertices += model->meshes[index].vertexCount;
    }
    return vertices;
}

static double PipelineStageAge(double now, double since)
{
    return since > 0.0 && now >= since ? now - since : 0.0;
}

const char *ChunkPipelineStageName(ChunkPipelineStage stage)
{
    switch (stage) {
    case CHUNK_PIPELINE_MISSING_CHUNK: return "missing_chunk";
    case CHUNK_PIPELINE_GENERATION_WAIT: return "generation_wait";
    case CHUNK_PIPELINE_GENERATION_QUEUED: return "generation_queued";
    case CHUNK_PIPELINE_GENERATION_RUNNING: return "generation_running";
    case CHUNK_PIPELINE_GENERATION_DONE: return "generation_done_wait_apply";
    case CHUNK_PIPELINE_IMPLICIT: return "implicit";
    case CHUNK_PIPELINE_DIRTY_WAIT: return "dirty_wait_submit";
    case CHUNK_PIPELINE_MESH_QUEUED: return "mesh_queued";
    case CHUNK_PIPELINE_MESH_RUNNING: return "mesh_running";
    case CHUNK_PIPELINE_MESH_DONE: return "mesh_done_wait_upload";
    case CHUNK_PIPELINE_READY: return "ready";
    default: return "unknown";
    }
}

bool ChunksGetSectionPipelineInfo(int cx, int sectionY, int cz,
                                  ChunkSectionPipelineInfo *outInfo)
{
    if (!outInfo || !SurfaceSectionInBounds(sectionY)) return false;
    *outInfo = (ChunkSectionPipelineInfo){
        .stage = CHUNK_PIPELINE_MISSING_CHUNK
    };
    Chunk *chunk = FindChunk(cx, cz);
    const ChunkSection *section = chunk
        ? ChunkGetSectionConst(chunk, sectionY) : NULL;
    if (chunk) {
        outInfo->chunkLoaded = chunk->loaded;
        outInfo->resolved = ChunkTerrainSectionIsResolved(chunk, sectionY);
    }
    if (section) {
        outInfo->materialized = true;
        outInfo->dirty = section->dirty;
        outInfo->currentStamp = section->dirtyStamp;
        outInfo->solidVertices = PipelineModelVertexCount(
            &section->model, section->hasModel);
        outInfo->waterVertices = PipelineModelVertexCount(
            &section->waterModel, section->hasWaterModel);
        outInfo->floraVertices = PipelineModelVertexCount(
            &section->floraModel, section->hasFloraModel);
    }

    double now = ChunkNowMs();
    const ChunkGenJob *generation = NULL;
    const MeshJob *mesh = NULL;
    pthread_mutex_lock(&genMutex);
    for (int index = 0; index < MAX_CHUNK_GEN_JOBS; index++) {
        const ChunkGenJob *candidate = &chunkGenJobs[index];
        if (!candidate->inUse || candidate->cx != cx ||
            candidate->cz != cz ||
            (candidate->scope == CHUNK_GEN_SCOPE_SECTION &&
             candidate->sectionY != sectionY)) continue;
        generation = candidate;
        break;
    }
    for (int index = 0; index < MAX_MESH_JOBS; index++) {
        const MeshJob *candidate = &meshJobs[index];
        if (!candidate->inUse || candidate->cx != cx ||
            candidate->cz != cz || candidate->sectionY != sectionY ||
            (chunk && candidate->chunkGeneration != chunk->generation)) {
            continue;
        }
        mesh = candidate;
        break;
    }

    if (generation) {
        if (generation->done) {
            outInfo->stage = CHUNK_PIPELINE_GENERATION_DONE;
            outInfo->stageAgeMs = PipelineStageAge(
                now, generation->completedAtMs);
        } else if (generation->running) {
            outInfo->stage = CHUNK_PIPELINE_GENERATION_RUNNING;
            outInfo->stageAgeMs = PipelineStageAge(
                now, generation->startedAtMs);
        } else {
            outInfo->stage = CHUNK_PIPELINE_GENERATION_QUEUED;
            outInfo->stageAgeMs = PipelineStageAge(
                now, generation->submittedAtMs);
        }
    } else if (!chunk || !chunk->loaded) {
        outInfo->stage = CHUNK_PIPELINE_MISSING_CHUNK;
    } else if (mesh) {
        outInfo->snapshotStamp = mesh->sectionStamp;
        if (mesh->done) {
            outInfo->stage = CHUNK_PIPELINE_MESH_DONE;
            outInfo->stageAgeMs = PipelineStageAge(
                now, mesh->completedAtMs);
        } else if (mesh->running) {
            outInfo->stage = CHUNK_PIPELINE_MESH_RUNNING;
            outInfo->stageAgeMs = PipelineStageAge(
                now, mesh->startedAtMs);
        } else {
            outInfo->stage = CHUNK_PIPELINE_MESH_QUEUED;
            outInfo->stageAgeMs = PipelineStageAge(
                now, mesh->submittedAtMs);
        }
    } else if (!section) {
        outInfo->stage = outInfo->resolved
            ? CHUNK_PIPELINE_IMPLICIT : CHUNK_PIPELINE_GENERATION_WAIT;
    } else if (section->dirty) {
        outInfo->stage = CHUNK_PIPELINE_DIRTY_WAIT;
        outInfo->stageAgeMs = PipelineStageAge(now, section->dirtySinceMs);
    } else {
        outInfo->stage = CHUNK_PIPELINE_READY;
    }
    pthread_mutex_unlock(&genMutex);
    return true;
}

bool ChunksGetWaterRenderDebugInfo(Vector3 position,
                                   ChunkWaterRenderDebugInfo *outInfo)
{
    if (!outInfo || !isfinite(position.x) || !isfinite(position.y) ||
        !isfinite(position.z) || position.x < (float)INT_MIN ||
        position.x > (float)INT_MAX || position.z < (float)INT_MIN ||
        position.z > (float)INT_MAX) {
        return false;
    }

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal((int)floorf(position.x), (int)floorf(position.z),
                      &cx, &cz, &lx, &lz);
    int blockY = (int)floorf(position.y);
    int sectionY = InHeight(blockY)
        ? SurfaceSectionYFromBlockY(blockY) : SURFACE_SECTION_MIN_Y - 1;
    *outInfo = (ChunkWaterRenderDebugInfo){
        .cx = cx,
        .cz = cz,
        .sectionY = sectionY
    };

    if (FindChunk(cx - 1, cz)) {
        outInfo->neighborLoadedMask |= CHUNK_WATER_NEIGHBOR_WEST;
    }
    if (FindChunk(cx + 1, cz)) {
        outInfo->neighborLoadedMask |= CHUNK_WATER_NEIGHBOR_EAST;
    }
    if (FindChunk(cx, cz - 1)) {
        outInfo->neighborLoadedMask |= CHUNK_WATER_NEIGHBOR_NORTH;
    }
    if (FindChunk(cx, cz + 1)) {
        outInfo->neighborLoadedMask |= CHUNK_WATER_NEIGHBOR_SOUTH;
    }

    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk) return false;
    outInfo->chunkLoaded = true;
    for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
         sectionIndex++) {
        const ChunkSection *section = chunk->sections[sectionIndex];
        if (!section->hasWaterModel ||
            !section->waterModel.meshes) {
            continue;
        }
        for (int meshIndex = 0; meshIndex < section->waterModel.meshCount;
             meshIndex++) {
            int triangles = section->waterModel.meshes[meshIndex].triangleCount;
            outInfo->triangleCount += triangles;
            if (section->sectionY == sectionY) {
                outInfo->sectionTriangleCount += triangles;
            }
        }
    }
    return true;
}

RenderResourceSnapshot ChunksGetRenderResourceSnapshot(void)
{
    RenderResourceSnapshot snapshot = { 0 };
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        const Chunk *chunk = &chunks[i];
        if (!chunk->loaded) continue;
        for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
             sectionIndex++) {
            const ChunkSection *section = chunk->sections[sectionIndex];
            if (section->hasModel) {
                RenderResourceSnapshotAddModel(&snapshot, &section->model,
                                               RENDER_RESOURCE_SOLID);
            }
            if (section->hasFloraModel) {
                RenderResourceSnapshotAddModel(&snapshot, &section->floraModel,
                                               RENDER_RESOURCE_FLORA);
            }
            if (section->hasWaterModel) {
                RenderResourceSnapshotAddModel(&snapshot, &section->waterModel,
                                               RENDER_RESOURCE_TRANSPARENT);
            }
        }
    }
    pthread_mutex_lock(&genMutex);
    UpdateQueuePeaksLocked();
    snapshot.pendingMeshSnapshotBytes = streamingStats.pendingMeshSnapshotBytes;
    snapshot.workerThreadsConfigured = 1;
    snapshot.workerThreadsStarted = genThread != 0 ? 1u : 0u;
    snapshot.workerThreadsActive = genWorkerActive ? 1u : 0u;
    pthread_mutex_unlock(&genMutex);
    return snapshot;
}

#ifdef CHUNKS_TESTING
bool ChunksTestSphereInFrustum(const Camera3D *camera, Vector3 center,
                               float radius, float aspect)
{
    return SphereInFrustumWithAspect(camera, center, radius, aspect);
}

void ChunksTestResetScheduler(void)
{
    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        ChunkClearBlockStorage(&chunks[i]);
    }
    memset(chunks, 0, sizeof(chunks));
    memset(chunkGenJobs, 0, sizeof(chunkGenJobs));
    memset(meshJobs, 0, sizeof(meshJobs));
    nextMeshQueueSequence = 0u;
    streamingStats = (ChunkStreamingStats){ 0 };
    genThread = pthread_self();
    genShutdown = false;
    genWorkerActive = false;
    pthread_mutex_unlock(&genMutex);
}

void ChunksTestConfigureChunk(int slotIndex, int cx, int cz, bool loaded, bool dirty)
{
    assert(slotIndex >= 0 && slotIndex < MAX_ACTIVE_CHUNKS);
    chunks[slotIndex] = (Chunk){ 0 };
    chunks[slotIndex].cx = cx;
    chunks[slotIndex].cz = cz;
    chunks[slotIndex].spherical = WorldIsSurfaceActive();
    if (chunks[slotIndex].spherical) {
        chunks[slotIndex].surfaceAddress = ChunkSurfaceAddressAt(cx, cz);
    }
    chunks[slotIndex].loaded = loaded;
    if (dirty) {
        ChunkSection *section = ChunkGetSection(&chunks[slotIndex], 0, true);
        if (section) section->dirty = true;
    }
}

bool ChunksTestChunkDirty(int slotIndex)
{
    assert(slotIndex >= 0 && slotIndex < MAX_ACTIVE_CHUNKS);
    for (int sectionIndex = 0;
         sectionIndex < chunks[slotIndex].sectionCount; sectionIndex++) {
        if (chunks[slotIndex].sections[sectionIndex]->dirty) return true;
    }
    return false;
}

int ChunksTestMeshJobSlot(int jobIndex)
{
    assert(jobIndex >= 0 && jobIndex < MAX_MESH_JOBS);
    return meshJobs[jobIndex].inUse ? meshJobs[jobIndex].slotIndex : -1;
}

int ChunksTestMeshJobSectionY(int jobIndex)
{
    assert(jobIndex >= 0 && jobIndex < MAX_MESH_JOBS);
    return meshJobs[jobIndex].inUse ? meshJobs[jobIndex].sectionY : -1;
}

int ChunksTestBuildWaterMeshJob(int jobIndex)
{
    assert(jobIndex >= 0 && jobIndex < MAX_MESH_JOBS);
    MeshJob *job = &meshJobs[jobIndex];
    assert(job->inUse);
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    Mesh mesh = { 0 };
    bool built = BuildSurfaceWaterMeshDataWithSnapshot(
        (const unsigned short (*)[CHUNK_SIZE])job->blocks,
        (const unsigned char *)job->waterVolumes,
        SURFACE_SECTION_HEIGHT,
        job->sectionY * SURFACE_SECTION_HEIGHT,
        job->cx, job->cz, faces, job->nearbyIndices, job->nearbyCount,
        &job->boundary, &mesh);
    int vertices = built ? mesh.vertexCount : 0;
    FreeMeshData(&mesh);
    return vertices;
}

void ChunksTestCompleteMeshJob(int jobIndex)
{
    assert(jobIndex >= 0 && jobIndex < MAX_MESH_JOBS);
    pthread_mutex_lock(&genMutex);
    if (meshJobs[jobIndex].inUse) {
        meshJobs[jobIndex].done = true;
        meshJobs[jobIndex].completedAtMs = ChunkNowMs();
    }
    pthread_mutex_unlock(&genMutex);
}

void ChunksTestSeedMeshJob(int jobIndex, int slotIndex, int cx, int cz,
                           int sectionY, bool done)
{
    assert(jobIndex >= 0 && jobIndex < MAX_MESH_JOBS);
    pthread_mutex_lock(&genMutex);
    meshJobs[jobIndex] = (MeshJob){
        .inUse = true,
        .done = done,
        .slotIndex = slotIndex,
        .cx = cx,
        .cz = cz,
        .sectionY = sectionY,
        .queueSequence = ++nextMeshQueueSequence,
        .submittedAtMs = ChunkNowMs(),
        .completedAtMs = done ? ChunkNowMs() : 0.0
    };
    UpdateQueuePeaksLocked();
    pthread_mutex_unlock(&genMutex);
}

int ChunksTestNextMeshJobIndex(void)
{
    pthread_mutex_lock(&genMutex);
    MeshJob *job = NextPendingMeshJob();
    int index = job ? (int)(job - meshJobs) : -1;
    pthread_mutex_unlock(&genMutex);
    return index;
}

void ChunksTestFillGenerationQueue(void)
{
    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        chunkGenJobs[i] = (ChunkGenJob){
            .inUse = true,
            .cx = 10000 + i,
            .cz = 10000
        };
    }
    UpdateQueuePeaksLocked();
    pthread_mutex_unlock(&genMutex);
}

int ChunksTestGenerationJobSectionY(int jobIndex)
{
    assert(jobIndex >= 0 && jobIndex < MAX_CHUNK_GEN_JOBS);
    pthread_mutex_lock(&genMutex);
    int sectionY = chunkGenJobs[jobIndex].inUse &&
        chunkGenJobs[jobIndex].scope == CHUNK_GEN_SCOPE_SECTION
        ? chunkGenJobs[jobIndex].sectionY : INT_MIN;
    pthread_mutex_unlock(&genMutex);
    return sectionY;
}

int ChunksTestGenerationJobSlot(int jobIndex)
{
    assert(jobIndex >= 0 && jobIndex < MAX_CHUNK_GEN_JOBS);
    pthread_mutex_lock(&genMutex);
    int slotIndex = chunkGenJobs[jobIndex].inUse &&
        chunkGenJobs[jobIndex].scope == CHUNK_GEN_SCOPE_SECTION
        ? chunkGenJobs[jobIndex].slotIndex : -1;
    pthread_mutex_unlock(&genMutex);
    return slotIndex;
}

bool ChunksTestGenerationJobSurfaceAddress(
    int jobIndex, SurfaceAddress *outAddress)
{
    assert(jobIndex >= 0 && jobIndex < MAX_CHUNK_GEN_JOBS);
    pthread_mutex_lock(&genMutex);
    bool spherical = chunkGenJobs[jobIndex].inUse &&
        chunkGenJobs[jobIndex].spherical;
    if (spherical && outAddress) {
        *outAddress = chunkGenJobs[jobIndex].surfaceAddress;
    }
    pthread_mutex_unlock(&genMutex);
    return spherical;
}

void ChunksTestRunGenerationJob(int jobIndex)
{
    assert(jobIndex >= 0 && jobIndex < MAX_CHUNK_GEN_JOBS);
    pthread_mutex_lock(&genMutex);
    ChunkGenJob *job = &chunkGenJobs[jobIndex];
    assert(job->inUse && !job->done);
    job->running = true;
    pthread_mutex_unlock(&genMutex);

    GenerateChunkJobPayload(job);

    pthread_mutex_lock(&genMutex);
    job->running = false;
    job->done = true;
    streamingStats.generationCompleted++;
    pthread_mutex_unlock(&genMutex);
}

int ChunksTestScheduleTerrainSections(Vector3 playerPosition,
                                      int effectiveRenderDistance)
{
    return ScheduleNearbyTerrainSections(playerPosition,
                                         effectiveRenderDistance);
}

int ChunksTestPruneTerrainSections(Vector3 playerPosition)
{
    int playerY = (int)floorf(playerPosition.y);
    return InHeight(playerY)
        ? PruneDistantNegativeTerrainSections(
              SurfaceSectionYFromBlockY(playerY))
        : 0;
}

int ChunksTestCancelDistantSectionJobs(Vector3 playerPosition)
{
    int playerY = (int)floorf(playerPosition.y);
    return InHeight(playerY)
        ? CancelDistantNegativeSectionJobs(
              SurfaceSectionYFromBlockY(playerY))
        : 0;
}

void ChunksTestSetGenerationJobRunning(int jobIndex, bool running)
{
    assert(jobIndex >= 0 && jobIndex < MAX_CHUNK_GEN_JOBS);
    pthread_mutex_lock(&genMutex);
    assert(chunkGenJobs[jobIndex].inUse);
    chunkGenJobs[jobIndex].running = running;
    pthread_mutex_unlock(&genMutex);
}

void ChunksTestSetMeshJobRunning(int jobIndex, bool running)
{
    assert(jobIndex >= 0 && jobIndex < MAX_MESH_JOBS);
    pthread_mutex_lock(&genMutex);
    assert(meshJobs[jobIndex].inUse);
    meshJobs[jobIndex].running = running;
    pthread_mutex_unlock(&genMutex);
}
#endif
