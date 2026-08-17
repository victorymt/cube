#include "space/space_internal.h"

#include "core/game_effects.h"

static SpaceChunk spaceChunks[MAX_SPACE_CHUNKS];
static BlockEdit spaceEdits[MAX_SPACE_EDITS];
static int spaceEditCount = 0;

typedef struct SpaceGenJob {
    bool inUse;
    bool started;
    bool done;
    bool canceled;
    int cx;
    int cz;
    int slotIndex;
    SpaceChunk result;
} SpaceGenJob;

static SpaceGenJob spaceGenJobs[MAX_SPACE_GEN_JOBS];
static pthread_mutex_t spaceGenMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t spaceGenCond = PTHREAD_COND_INITIALIZER;
static pthread_t spaceGenThread;
static bool spaceGenThreadStarted = false;
static bool spaceGenShutdown = false;
static bool spaceGenWorkerActive = false;

static void GenerateSpaceChunk(SpaceChunk *chunk, int cx, int cz);
static void *SpaceGenWorker(void *arg);
static void CancelSpaceGenForSlot(int slotIndex);
static void CancelAllSpaceGenJobs(void);

const SpaceChunk *SpaceChunksView(void)
{
    return spaceChunks;
}

void SpaceInit(void)
{
    memset(spaceChunks, 0, sizeof(spaceChunks));
    memset(spaceGenJobs, 0, sizeof(spaceGenJobs));
    spaceEditCount = 0;

    pthread_mutex_lock(&spaceGenMutex);
    spaceGenShutdown = false;
    spaceGenWorkerActive = false;
    pthread_mutex_unlock(&spaceGenMutex);

    if (pthread_create(&spaceGenThread, NULL, SpaceGenWorker, NULL) == 0) {
        spaceGenThreadStarted = true;
    }
}

void SpaceShutdown(void)
{
    if (!spaceGenThreadStarted) return;

    pthread_mutex_lock(&spaceGenMutex);
    spaceGenShutdown = true;
    for (int i = 0; i < MAX_SPACE_GEN_JOBS; i++) {
        if (!spaceGenJobs[i].inUse) continue;
        spaceGenJobs[i].canceled = true;
    }
    pthread_cond_broadcast(&spaceGenCond);
    pthread_mutex_unlock(&spaceGenMutex);

    pthread_join(spaceGenThread, NULL);
    spaceGenThreadStarted = false;
    spaceGenWorkerActive = false;
    memset(spaceGenJobs, 0, sizeof(spaceGenJobs));
}

static SpaceChunk *FindSpaceChunk(int cx, int cz)
{
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (spaceChunks[i].loaded && spaceChunks[i].cx == cx && spaceChunks[i].cz == cz) return &spaceChunks[i];
    }
    return NULL;
}

static bool FindPendingSpaceChunk(int cx, int cz)
{
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (spaceChunks[i].generating && spaceChunks[i].cx == cx && spaceChunks[i].cz == cz) return true;
    }
    return false;
}

static SpaceChunk *AllocateSpaceChunkSlot(int cx, int cz)
{
    SpaceChunk *empty = NULL;
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (!spaceChunks[i].loaded && !spaceChunks[i].generating) {
            empty = &spaceChunks[i];
            break;
        }
    }
    if (!empty) return NULL;
    memset(empty, 0, sizeof(*empty));
    empty->cx = cx;
    empty->cz = cz;
    return empty;
}

static void UnloadSpaceChunkModel(SpaceChunk *chunk)
{
    if (chunk->hasModel) {
        UnloadModel(chunk->model);
        chunk->hasModel = false;
    }
    if (chunk->hasWaterModel) {
        UnloadModel(chunk->waterModel);
        chunk->hasWaterModel = false;
    }
}

static void ApplySpaceEditsToChunk(SpaceChunk *chunk)
{
    for (int i = 0; i < spaceEditCount; i++) {
        const BlockEdit *edit = &spaceEdits[i];
        if (edit->y < SPACE_LAYER_Y || edit->y >= SPACE_LAYER_TOP) continue;
        int localX = SpaceGlobalToLocalX(edit->x);
        int localZ = SpaceGlobalToLocalZ(edit->z);
        int editCx = 0;
        int editCz = 0;
        int editLx = 0;
        int editLz = 0;
        WorldToChunkLocal(localX, localZ, &editCx, &editCz, &editLx, &editLz);
        if (editCx == chunk->cx && editCz == chunk->cz) {
            chunk->blocks[editLx][edit->y - SPACE_LAYER_Y][editLz] = (unsigned short)edit->type;
        }
    }
}


static void GenerateSpaceChunk(SpaceChunk *chunk, int cx, int cz)
{
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                chunk->blocks[lx][ly][lz] = (unsigned short)BLOCK_AIR;
            }
        }
    }

    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(SpaceLocalToGlobalX(startX - 8), ASTEROID_SPACING);
    int maxAnchorX = FloorDivInt(SpaceLocalToGlobalX(startX + CHUNK_SIZE + 8),
                                 ASTEROID_SPACING);
    int minAnchorZ = FloorDivInt(SpaceLocalToGlobalZ(startZ - 8), ASTEROID_SPACING);
    int maxAnchorZ = FloorDivInt(SpaceLocalToGlobalZ(startZ + CHUNK_SIZE + 8),
                                 ASTEROID_SPACING);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            if (WorldHash2D(anchorX, anchorZ) % 100u >= ASTEROID_PROBABILITY) continue;

            int wx = SpaceGlobalToLocalX(ClampCoordinate((int64_t)anchorX * ASTEROID_SPACING));
            int wz = SpaceGlobalToLocalZ(ClampCoordinate((int64_t)anchorZ * ASTEROID_SPACING));
            if (SpacePointInSolarSystemBubble(wx, wz)) continue;
            int wy = SPACE_LAYER_Y + 8 +
                     (int)(WorldHash2D(anchorX + 3, anchorZ) %
                           (unsigned int)(SPACE_LAYER_HEIGHT - 16));
            int radius = 3 + (int)(WorldHash2D(anchorX, anchorZ + 7) % 5u);
            float radiusSqr = (float)(radius * radius);
            float shellSqr = (float)((radius - 1) * (radius - 1));

            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
                    for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                        if (chunk->blocks[lx][ly][lz] != 0) continue;

                        int bx = startX + lx;
                        int by = SPACE_LAYER_Y + ly;
                        int bz = startZ + lz;
                        float dx = (float)(bx - wx);
                        float dy = (float)(by - wy);
                        float dz = (float)(bz - wz);
                        float distSqr = dx * dx + dy * dy + dz * dz;
                        if (distSqr >= radiusSqr) continue;

                        BlockType type = (distSqr >= shellSqr) ? BLOCK_MOON_SAND : BLOCK_MOON_ROCK;
                        if (WorldHash3D(SpaceLocalToGlobalX(bx), by,
                                        SpaceLocalToGlobalZ(bz)) % 89u == 0u) {
                            type = BLOCK_METEORITE;
                        }
                        chunk->blocks[lx][ly][lz] = (unsigned short)type;
                    }
                }
            }
        }
    }

    chunk->loaded = true;
    chunk->dirty = true;
}

static SpaceGenJob *NextSpaceGenJobLocked(void)
{
    for (int i = 0; i < MAX_SPACE_GEN_JOBS; i++) {
        if (spaceGenJobs[i].inUse && !spaceGenJobs[i].started && !spaceGenJobs[i].done) {
            return &spaceGenJobs[i];
        }
    }
    return NULL;
}

static void *SpaceGenWorker(void *arg)
{
    (void)arg;

    for (;;) {
        pthread_mutex_lock(&spaceGenMutex);
        SpaceGenJob *job = NULL;
        while (!spaceGenShutdown && !(job = NextSpaceGenJobLocked())) {
            pthread_cond_wait(&spaceGenCond, &spaceGenMutex);
        }
        if (spaceGenShutdown) {
            pthread_mutex_unlock(&spaceGenMutex);
            break;
        }

        job->started = true;
        spaceGenWorkerActive = true;
        int cx = job->cx;
        int cz = job->cz;
        pthread_mutex_unlock(&spaceGenMutex);

        GenerateSpaceChunk(&job->result, cx, cz);

        pthread_mutex_lock(&spaceGenMutex);
        job->done = true;
        spaceGenWorkerActive = false;
        pthread_cond_broadcast(&spaceGenCond);
        pthread_mutex_unlock(&spaceGenMutex);
    }

    return NULL;
}

static bool SubmitSpaceGenJob(SpaceChunk *chunk)
{
    if (!spaceGenThreadStarted) return false;

    pthread_mutex_lock(&spaceGenMutex);
    SpaceGenJob *job = NULL;
    for (int i = 0; i < MAX_SPACE_GEN_JOBS; i++) {
        if (!spaceGenJobs[i].inUse) {
            job = &spaceGenJobs[i];
            break;
        }
    }
    if (!job) {
        pthread_mutex_unlock(&spaceGenMutex);
        return false;
    }

    *job = (SpaceGenJob){
        .inUse = true,
        .cx = chunk->cx,
        .cz = chunk->cz,
        .slotIndex = (int)(chunk - spaceChunks),
        .result = { .cx = chunk->cx, .cz = chunk->cz }
    };
    pthread_cond_signal(&spaceGenCond);
    pthread_mutex_unlock(&spaceGenMutex);
    return true;
}

static void CancelSpaceGenForSlot(int slotIndex)
{
    pthread_mutex_lock(&spaceGenMutex);
    for (int i = 0; i < MAX_SPACE_GEN_JOBS; i++) {
        SpaceGenJob *job = &spaceGenJobs[i];
        if (!job->inUse || job->slotIndex != slotIndex) continue;
        job->canceled = true;
        if (!job->started) job->done = true;
    }
    pthread_cond_broadcast(&spaceGenCond);
    pthread_mutex_unlock(&spaceGenMutex);
}

static void CancelAllSpaceGenJobs(void)
{
    pthread_mutex_lock(&spaceGenMutex);
    for (int i = 0; i < MAX_SPACE_GEN_JOBS; i++) {
        SpaceGenJob *job = &spaceGenJobs[i];
        if (!job->inUse) continue;
        job->canceled = true;
        if (!job->started) job->done = true;
    }
    pthread_cond_broadcast(&spaceGenCond);
    pthread_mutex_unlock(&spaceGenMutex);
}

static void DrainCanceledSpaceGenJobs(void)
{
    if (!spaceGenThreadStarted) return;

    pthread_mutex_lock(&spaceGenMutex);
    for (;;) {
        bool waiting = false;
        for (int i = 0; i < MAX_SPACE_GEN_JOBS; i++) {
            SpaceGenJob *job = &spaceGenJobs[i];
            if (!job->inUse) continue;
            if (job->done) {
                job->inUse = false;
                continue;
            }
            waiting = true;
        }
        if (!waiting) break;
        pthread_cond_wait(&spaceGenCond, &spaceGenMutex);
    }
    pthread_mutex_unlock(&spaceGenMutex);
}

void SpaceProcessFinishedGenJobs(void)
{
    for (;;) {
        pthread_mutex_lock(&spaceGenMutex);
        SpaceGenJob *job = NULL;
        for (int i = 0; i < MAX_SPACE_GEN_JOBS; i++) {
            if (spaceGenJobs[i].inUse && spaceGenJobs[i].done) {
                job = &spaceGenJobs[i];
                break;
            }
        }
        if (!job) {
            pthread_mutex_unlock(&spaceGenMutex);
            return;
        }

        int slotIndex = job->slotIndex;
        if (!job->canceled && slotIndex >= 0 && slotIndex < MAX_SPACE_CHUNKS) {
            SpaceChunk *chunk = &spaceChunks[slotIndex];
            if (chunk->generating && chunk->cx == job->cx && chunk->cz == job->cz) {
                memcpy(chunk->blocks, job->result.blocks, sizeof(chunk->blocks));
                chunk->loaded = true;
                chunk->generating = false;
                chunk->dirty = true;
                ApplySpaceEditsToChunk(chunk);
            }
        } else if (slotIndex >= 0 && slotIndex < MAX_SPACE_CHUNKS) {
            SpaceChunk *chunk = &spaceChunks[slotIndex];
            if (chunk->generating && chunk->cx == job->cx && chunk->cz == job->cz) {
                chunk->generating = false;
            }
        }

        job->inUse = false;
        pthread_cond_broadcast(&spaceGenCond);
        pthread_mutex_unlock(&spaceGenMutex);
    }
}

static void SpaceRememberEdit(int x, int y, int z, BlockType type)
{
    int globalX = SpaceLocalToGlobalX(x);
    int globalZ = SpaceLocalToGlobalZ(z);
    for (int i = 0; i < spaceEditCount; i++) {
        if (spaceEdits[i].x == globalX && spaceEdits[i].y == y &&
            spaceEdits[i].z == globalZ) {
            spaceEdits[i].type = type;
            return;
        }
    }
    if (spaceEditCount < MAX_SPACE_EDITS) {
        spaceEdits[spaceEditCount++] = (BlockEdit){ globalX, y, globalZ, type };
    }
}

static void RebuildSpaceChunkMesh(SpaceChunk *chunk)
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

    UnloadSpaceChunkModel(chunk);

    Mesh solidMesh = { 0 };
    Mesh waterMesh = { 0 };
    bool hasSolid = BuildMeshData((const unsigned short (*)[CHUNK_SIZE])chunk->blocks,
                                  SPACE_LAYER_HEIGHT, SPACE_LAYER_Y,
                                  chunk->cx, chunk->cz, false, faces,
                                  nearbyTorchIndices, nearbyTorchCount, &solidMesh);
    bool hasWater = BuildMeshData((const unsigned short (*)[CHUNK_SIZE])chunk->blocks,
                                  SPACE_LAYER_HEIGHT, SPACE_LAYER_Y,
                                  chunk->cx, chunk->cz, true, faces,
                                  nearbyTorchIndices, nearbyTorchCount, &waterMesh);

    if (hasSolid) {
        UploadMesh(&solidMesh, false);
        chunk->model = LoadModelFromMesh(solidMesh);
        SetMaterialTexture(&chunk->model.materials[0], MATERIAL_MAP_DIFFUSE,
                           ChunksBlockAtlas());
        chunk->hasModel = true;
    }
    if (hasWater) {
        UploadMesh(&waterMesh, false);
        chunk->waterModel = LoadModelFromMesh(waterMesh);
        SetMaterialTexture(&chunk->waterModel.materials[0],
                           MATERIAL_MAP_DIFFUSE, ChunksBlockAtlas());
        chunk->hasWaterModel = true;
    }
    chunk->dirty = false;
}

void UpdateSpaceChunks(Vector3 playerPosition, int groundRenderDistance, int generationPerFrame)
{
    int renderDist = SPACE_RENDER_DISTANCE_CHUNKS;
    if (groundRenderDistance < renderDist) renderDist = groundRenderDistance;

    int playerCx = 0;
    int playerCz = 0;
    int playerLx = 0;
    int playerLz = 0;
    WorldToChunkLocal((int)floorf(playerPosition.x), (int)floorf(playerPosition.z),
                      &playerCx, &playerCz, &playerLx, &playerLz);

    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (!spaceChunks[i].loaded && !spaceChunks[i].generating) continue;
        if (abs(spaceChunks[i].cx - playerCx) > renderDist ||
            abs(spaceChunks[i].cz - playerCz) > renderDist) {
            if (spaceChunks[i].generating) CancelSpaceGenForSlot(i);
            if (spaceChunks[i].loaded) UnloadSpaceChunkModel(&spaceChunks[i]);
            spaceChunks[i].loaded = false;
            spaceChunks[i].generating = false;
            spaceChunks[i].dirty = false;
        }
    }

    if (playerPosition.y < 50.0f) return;

    int missingChunks[MAX_SPACE_CHUNKS][2];
    int missingCount = 0;
    for (int dz = -renderDist; dz <= renderDist; dz++) {
        for (int dx = -renderDist; dx <= renderDist; dx++) {
            int cx = playerCx + dx;
            int cz = playerCz + dz;
            if (FindSpaceChunk(cx, cz) || FindPendingSpaceChunk(cx, cz)) continue;

            int insert = missingCount;
            int distance = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
            while (insert > 0) {
                int prevDx = missingChunks[insert - 1][0] - playerCx;
                int prevDz = missingChunks[insert - 1][1] - playerCz;
                int prevDistance = abs(prevDx) > abs(prevDz) ? abs(prevDx) : abs(prevDz);
                if (prevDistance <= distance) break;
                missingChunks[insert][0] = missingChunks[insert - 1][0];
                missingChunks[insert][1] = missingChunks[insert - 1][1];
                insert--;
            }
            missingChunks[insert][0] = cx;
            missingChunks[insert][1] = cz;
            missingCount++;
        }
    }

    int generated = 0;
    for (int i = 0; i < missingCount && generated < generationPerFrame; i++) {
        int cx = missingChunks[i][0];
        int cz = missingChunks[i][1];
        SpaceChunk *chunk = AllocateSpaceChunkSlot(cx, cz);
        if (!chunk) break;
        chunk->cx = cx;
        chunk->cz = cz;
        chunk->generating = true;
        if (!SubmitSpaceGenJob(chunk)) {
            if (spaceGenThreadStarted) {
                chunk->generating = false;
                break;
            }
            GenerateSpaceChunk(chunk, cx, cz);
            ApplySpaceEditsToChunk(chunk);
            chunk->loaded = true;
            chunk->generating = false;
            chunk->dirty = true;
        }
        generated++;
    }

    int rebuilt = 0;
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (!spaceChunks[i].loaded || !spaceChunks[i].dirty) continue;
        RebuildSpaceChunkMesh(&spaceChunks[i]);
        if (++rebuilt >= SPACE_MESH_REBUILDS_PER_FRAME) break;
    }
}

BlockType SpaceBlockAt(int x, int y, int z)
{
    if (y < SPACE_LAYER_Y || y >= SPACE_LAYER_TOP) return BLOCK_AIR;

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    SpaceChunk *chunk = FindSpaceChunk(cx, cz);
    if (!chunk) return BLOCK_AIR;
    return (BlockType)chunk->blocks[lx][y - SPACE_LAYER_Y][lz];
}

bool SpaceBlockReadyAt(int x, int y, int z)
{
    if (y < SPACE_LAYER_Y || y >= SPACE_LAYER_TOP) return true;

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    SpaceChunk *chunk = FindSpaceChunk(cx, cz);
    return chunk != NULL && chunk->loaded && !chunk->generating;
}

void SpaceSetBlock(int x, int y, int z, BlockType type)
{
    if (y < SPACE_LAYER_Y || y >= SPACE_LAYER_TOP) return;

    SpaceRememberEdit(x, y, z, type);

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    SpaceChunk *chunk = FindSpaceChunk(cx, cz);
    if (!chunk) return;
    chunk->blocks[lx][y - SPACE_LAYER_Y][lz] = (unsigned short)type;
    chunk->dirty = true;
}

bool SpaceSaveEdits(FILE *file)
{
    if (!file) return false;
    uint32_t count = (uint32_t)spaceEditCount;
    if (fwrite(&count, sizeof(count), 1, file) != 1) return false;
    if (spaceEditCount > 0) {
        if (fwrite(spaceEdits, sizeof(BlockEdit), (size_t)spaceEditCount, file) !=
            (size_t)spaceEditCount) return false;
    }
    return true;
}

bool SpaceLoadEdits(FILE *file, int storedLayerY)
{
    if (!file) return false;

    uint32_t count = 0;
    if (fread(&count, sizeof(count), 1, file) != 1) return true;

    if (count > MAX_SPACE_EDITS) return false;

    BlockEdit *loaded = count > 0 ? malloc((size_t)count * sizeof(*loaded)) : NULL;
    if (count > 0 && !loaded) return false;
    for (uint32_t i = 0; i < count; i++) {
        if (fread(&loaded[i], sizeof(loaded[i]), 1, file) != 1) {
            free(loaded);
            return false;
        }
        int64_t storedLayerTop = (int64_t)storedLayerY + SPACE_LAYER_HEIGHT;
        int64_t migratedY = (int64_t)loaded[i].y - (int64_t)storedLayerY +
                            (int64_t)SPACE_LAYER_Y;
        if ((int64_t)loaded[i].y < storedLayerY ||
            (int64_t)loaded[i].y >= storedLayerTop ||
            migratedY < SPACE_LAYER_Y || migratedY >= SPACE_LAYER_TOP ||
            !IsValidBlockType(loaded[i].type)) {
            free(loaded);
            return false;
        }
        loaded[i].y = (int)migratedY;
    }
    spaceEditCount = (int)count;
    if (count > 0) memcpy(spaceEdits, loaded, (size_t)count * sizeof(*loaded));
    free(loaded);
    return true;
}

void UnloadAllSpaceChunks(void)
{
    CancelAllSpaceGenJobs();
    DrainCanceledSpaceGenJobs();
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (spaceChunks[i].loaded) UnloadSpaceChunkModel(&spaceChunks[i]);
        spaceChunks[i].loaded = false;
        spaceChunks[i].generating = false;
        spaceChunks[i].dirty = false;
    }
}

void SpaceReset(void)
{
    UnloadAllSpaceChunks();
    spaceEditCount = 0;
    solarElapsedSimulationTime = 0.0;
    spaceLastLoadError = SPACE_LOAD_ERROR_NONE;
    SpaceQueryCacheClear();
    SpaceResetOrigin();
    PlanetWorldReset();
    HomeWorldReset();
}

bool SpaceRebasePlayer(Player *player)
{
    if (!player || HomeWorldSurfaceIsActive() || PlanetWorldIsActive()) return false;
    if (fabsf(player->position.x) < (float)SPACE_REBASE_THRESHOLD &&
        fabsf(player->position.z) < (float)SPACE_REBASE_THRESHOLD) {
        return false;
    }

    int64_t stepX = (int64_t)llroundf(player->position.x / (float)STAR_SYSTEM_SPACING) *
                    STAR_SYSTEM_SPACING;
    int64_t stepZ = (int64_t)llroundf(player->position.z / (float)STAR_SYSTEM_SPACING) *
                    STAR_SYSTEM_SPACING;
    if (stepX == 0 && stepZ == 0) return false;

    int64_t nextOriginX = (int64_t)spaceOriginX + stepX;
    int64_t nextOriginZ = (int64_t)spaceOriginZ + stepZ;
    if (nextOriginX > INT_MAX || nextOriginX < INT_MIN ||
        nextOriginZ > INT_MAX || nextOriginZ < INT_MIN) {
        return false;
    }

    // Wait for workers before changing the frame they use for procedural data.
    UnloadAllSpaceChunks();
    spaceOriginX = (int)nextOriginX;
    spaceOriginZ = (int)nextOriginZ;
    player->position.x -= (float)stepX;
    player->position.z -= (float)stepZ;
    RebuildTorchList();
    SpaceRebuildTorchList();
    return true;
}

int GetActiveSpaceChunkCount(void)
{
    int count = 0;
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (spaceChunks[i].loaded) count++;
    }
    return count;
}

RenderResourceSnapshot SpaceGetRenderResourceSnapshot(void)
{
    RenderResourceSnapshot snapshot = { 0 };
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        const SpaceChunk *chunk = &spaceChunks[i];
        if (!chunk->loaded) continue;
        if (chunk->hasModel) {
            RenderResourceSnapshotAddModel(&snapshot, &chunk->model,
                                           RENDER_RESOURCE_SOLID);
        }
        if (chunk->hasWaterModel) {
            RenderResourceSnapshotAddModel(&snapshot, &chunk->waterModel,
                                           RENDER_RESOURCE_TRANSPARENT);
        }
    }
    pthread_mutex_lock(&spaceGenMutex);
    snapshot.workerThreadsConfigured = 1;
    snapshot.workerThreadsStarted = spaceGenThreadStarted ? 1u : 0u;
    snapshot.workerThreadsActive = spaceGenWorkerActive ? 1u : 0u;
    pthread_mutex_unlock(&spaceGenMutex);
    return snapshot;
}

void SpaceRebuildTorchList(void)
{
    for (int i = 0; i < spaceEditCount; i++) {
        if (spaceEdits[i].type == BLOCK_TORCH) {
            TorchLightAdd(SpaceGlobalToLocalX(spaceEdits[i].x), spaceEdits[i].y,
                          SpaceGlobalToLocalZ(spaceEdits[i].z));
        }
    }
}

int GetSpaceEditCount(void)
{
    return spaceEditCount;
}

void SpaceUpdateSolarGlow(Vector3 playerPosition)
{
    SpaceBodyInfo bodies[STAR_NAVIGATION_MAX_SYSTEMS];
    int bodyCount = SpaceBodiesNear(playerPosition, SOLAR_GLOW_QUERY_RADIUS, bodies,
                                     STAR_NAVIGATION_MAX_SYSTEMS);
    for (int i = 0; i < bodyCount; i++) {
        if (!bodies[i].isStar) continue;

        float dist = bodies[i].dist;
        int count = dist < 24.0f ? 3 : 1;
        Color glow = SpectrumColor(bodies[i].spectrum);
        float spread = fmaxf(3.0f, bodies[i].spaceProxyRadius * 0.55f);
        for (int k = 0; k < count; k++) {
            Vector3 offset = {
                ((float)rand() / (float)RAND_MAX - 0.5f) * spread,
                ((float)rand() / (float)RAND_MAX - 0.5f) * spread,
                ((float)rand() / (float)RAND_MAX - 0.5f) * spread
            };
            GameEffectsEmitParticleOne(
                Vector3Add(bodies[i].center, offset),
                (Vector3){
                    ((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f,
                    0.2f + (float)rand() / (float)RAND_MAX * 0.5f,
                    ((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f
                },
                glow, (Vector3){ 0.14f, 0.14f, 0.14f }, 1.8f, 0.0f);
        }
    }
}
