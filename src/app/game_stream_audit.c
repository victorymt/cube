#include "app/game_stream_audit.h"

#include "app/game_runtime.h"
#include "core/debug_control.h"
#include "world/chunks.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <math.h>
#include <stdio.h>

typedef struct GameStreamAuditExpected {
    int solid;
    int water;
    int flora;
} GameStreamAuditExpected;

static int ModelVertexCount(const Model *model, bool present)
{
    if (!present || !model || model->meshCount <= 0 || !model->meshes) {
        return 0;
    }
    int vertices = 0;
    for (int index = 0; index < model->meshCount; index++) {
        vertices += model->meshes[index].vertexCount;
    }
    return vertices;
}

static GameStreamAuditExpected CountExpectedLayers(
    int cx, int sectionY, int cz)
{
    static const int neighbors[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    GameStreamAuditExpected expected = { 0 };
    const Chunk *chunk = FindChunk(cx, cz);
    int startX = cx * CHUNK_SIZE;
    int startY = sectionY * SURFACE_SECTION_HEIGHT;
    int startZ = cz * CHUNK_SIZE;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int ly = 0; ly < SURFACE_SECTION_HEIGHT; ly++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int x = startX + lx;
                int y = startY + ly;
                int z = startZ + lz;
                BlockType block = GetBlockAt(x, y, z);
                if (block == BLOCK_AIR ||
                    block == BLOCK_SPACESHIP_OCCUPIED) continue;
                if (IsPlantBlock(block) || ChunkFloraStructureOwnsBlock(
                        chunk, x, y, z, block)) {
                    expected.flora++;
                    continue;
                }
                bool water = IsWaterBlock(block);
                bool exposed = false;
                for (int side = 0; side < 6; side++) {
                    BlockType neighbor = GetBlockAt(
                        x + neighbors[side][0], y + neighbors[side][1],
                        z + neighbors[side][2]);
                    bool waterFaceVisible =
                        neighbor == BLOCK_AIR ||
                        neighbor == BLOCK_SPACESHIP_OCCUPIED ||
                        (IsTranslucentBlock(neighbor) &&
                         !IsWaterBlock(neighbor));
                    if (water ? waterFaceVisible :
                        (neighbor == BLOCK_AIR ||
                         neighbor == BLOCK_SPACESHIP_OCCUPIED ||
                         IsTranslucentBlock(neighbor))) {
                        exposed = true;
                        break;
                    }
                }
                if (!exposed) continue;
                if (water) expected.water++;
                else expected.solid++;
            }
        }
    }
    return expected;
}

static GameStreamAuditSnapshot SnapshotAt(int cx, int sectionY, int cz)
{
    const Chunk *chunk = FindChunk(cx, cz);
    const ChunkSection *section = chunk
        ? ChunkGetSectionConst(chunk, sectionY) : NULL;
    return (GameStreamAuditSnapshot){
        .cx = cx,
        .cz = cz,
        .sectionY = sectionY,
        .generation = chunk ? chunk->generation : 0u,
        .dirtyStamp = section ? section->dirtyStamp : 0u,
        .solidVertices = section
            ? ModelVertexCount(&section->model, section->hasModel) : 0,
        .waterVertices = section
            ? ModelVertexCount(&section->waterModel,
                               section->hasWaterModel) : 0,
        .floraVertices = section
            ? ModelVertexCount(&section->floraModel,
                               section->hasFloraModel) : 0,
        .loaded = chunk && chunk->loaded,
        .resolved = chunk &&
            ChunkTerrainSectionIsResolved(chunk, sectionY),
        .materialized = section != NULL,
        .dirty = section && section->dirty
    };
}

static bool SnapshotEqual(const GameStreamAuditSnapshot *left,
                          const GameStreamAuditSnapshot *right)
{
    return left->generation == right->generation &&
           left->dirtyStamp == right->dirtyStamp &&
           left->solidVertices == right->solidVertices &&
           left->waterVertices == right->waterVertices &&
           left->floraVertices == right->floraVertices &&
           left->loaded == right->loaded &&
           left->resolved == right->resolved &&
           left->materialized == right->materialized &&
           left->dirty == right->dirty;
}

static void AddSnapshot(GameStreamAuditState *audit,
                        GameStreamAuditSnapshot snapshot)
{
    if (audit->snapshotCount >= GAME_STREAM_AUDIT_MAX_SNAPSHOTS) return;
    audit->snapshots[audit->snapshotCount++] = snapshot;
}

static void AddIssue(GameStreamAuditState *audit,
                     GameStreamAuditIssue issue)
{
    if (audit->issuesEmitted < GAME_STREAM_AUDIT_MAX_ISSUES) {
        audit->issues[audit->issuesEmitted++] = issue;
    }
    audit->issuesTotal++;
}

static bool LayerMissing(int expected, int vertices);

static void AddLayerIssue(GameStreamAuditState *audit,
                          const GameStreamAuditSnapshot *snapshot,
                          GameStreamAuditLayer layer,
                          int expected, int vertices)
{
    if (!LayerMissing(expected, vertices)) return;
    AddIssue(audit, (GameStreamAuditIssue){
        .cx = snapshot->cx,
        .cz = snapshot->cz,
        .sectionY = snapshot->sectionY,
        .expected = expected,
        .vertices = vertices,
        .dirtyStamp = snapshot->dirtyStamp,
        .layer = layer,
        .loaded = snapshot->loaded,
        .resolved = snapshot->resolved,
        .materialized = snapshot->materialized,
        .dirty = snapshot->dirty
    });
}

static bool LayerMissing(int expected, int vertices)
{
    return expected > 0 && vertices <= 0;
}

static const char *LayerName(GameStreamAuditLayer layer)
{
    switch (layer) {
    case GAME_STREAM_AUDIT_LAYER_SOLID: return "solid";
    case GAME_STREAM_AUDIT_LAYER_WATER: return "water";
    case GAME_STREAM_AUDIT_LAYER_FLORA: return "flora";
    default: return "chunk";
    }
}

static void FinishAudit(GameRuntime *game)
{
    GameStreamAuditState *audit = &game->streamAudit;
    for (int index = 0; index < audit->snapshotCount; index++) {
        GameStreamAuditSnapshot current = SnapshotAt(
            audit->snapshots[index].cx,
            audit->snapshots[index].sectionY,
            audit->snapshots[index].cz);
        if (!SnapshotEqual(&audit->snapshots[index], &current)) {
            audit->staleSections++;
        }
    }
    for (int index = 0; index < audit->issuesEmitted; index++) {
        const GameStreamAuditIssue *issue = &audit->issues[index];
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL stream issue chunk=%d,%d section=%d "
            "layer=%s loaded=%d resolved=%d materialized=%d "
            "expected=%d vertices=%d dirty=%d stamp=%u\n",
            issue->cx, issue->cz, issue->sectionY,
            LayerName(issue->layer), issue->loaded ? 1 : 0,
            issue->resolved ? 1 : 0, issue->materialized ? 1 : 0,
            issue->expected, issue->vertices, issue->dirty ? 1 : 0,
            issue->dirtyStamp);
    }
    ChunkStreamingStats stats = ChunksGetStreamingStats();
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL stream audit result=%s focus=%d,%d,%d radius=%d "
        "chunks_loaded=%d chunks_missing=%d sections_resolved=%d "
        "sections_materialized=%d sections_implicit_only=%d "
        "sections_unmeshed=%d sections_modeled=%d sections_dirty=%d "
        "issues_total=%d issues_emitted=%d truncated=%d "
        "elapsed_frames=%d stale_sections=%d active_chunks=%d "
        "pending_gen=%d pending_mesh=%d gen_submitted=%llu "
        "gen_completed=%llu gen_canceled=%llu mesh_submitted=%llu "
        "mesh_completed=%llu mesh_canceled=%llu mesh_uploaded=%llu "
        "upload_deferrals=%llu\n",
        audit->staleSections > 0 ? "stale_rerun_required" : "complete",
        audit->focusCx, audit->focusSectionY, audit->focusCz,
        audit->radius, audit->loadedChunks, audit->missingChunks,
        audit->resolvedSections, audit->materializedSections,
        audit->implicitOnlySections, audit->unmeshedSections,
        audit->modeledSections, audit->dirtySections,
        audit->issuesTotal, audit->issuesEmitted,
        audit->issuesTotal > audit->issuesEmitted ? 1 : 0,
        audit->elapsedFrames, audit->staleSections,
        GetActiveChunkCount(), GetPendingGenJobCount(),
        GetPendingMeshJobCount(),
        (unsigned long long)stats.generationSubmitted,
        (unsigned long long)stats.generationCompleted,
        (unsigned long long)stats.generationCanceled,
        (unsigned long long)stats.meshSubmitted,
        (unsigned long long)stats.meshCompleted,
        (unsigned long long)stats.meshCanceled,
        (unsigned long long)stats.uploadedMeshes,
        (unsigned long long)stats.uploadBudgetDeferrals);
    audit->active = false;
}

static void AdvanceAudit(GameStreamAuditState *audit)
{
    audit->vertical++;
    if (audit->vertical <= 1) return;
    audit->vertical = -1;
    audit->dx++;
    if (audit->dx <= audit->radius) return;
    audit->dx = -audit->radius;
    audit->dz++;
}

static bool AdvanceStreamWait(GameStreamAuditState *audit, bool settled)
{
    audit->wait.elapsedFrames++;
    if (settled) audit->wait.settledFrames++;
    else audit->wait.settledFrames = 0;
    return audit->wait.settledFrames >= 2u ||
           audit->wait.elapsedFrames >= audit->wait.timeoutFrames;
}

static bool StreamWaitStageSettled(ChunkPipelineStage stage)
{
    return stage == CHUNK_PIPELINE_READY || stage == CHUNK_PIPELINE_IMPLICIT;
}

static int RequestStreamWaitSections(const GameStreamAuditState *audit)
{
    int requests = 0;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dx = -1; dx <= 1; dx++) {
            for (int vertical = -1; vertical <= 1; vertical++) {
                int sectionY = audit->wait.focusSectionY + vertical;
                if (!SurfaceSectionInBounds(sectionY)) continue;
                if (RequestChunkTerrainSection(
                        audit->wait.focusCx + dx, sectionY,
                        audit->wait.focusCz + dz)) {
                    requests++;
                }
            }
        }
    }
    return requests;
}

static int PendingStreamWaitSections(const GameStreamAuditState *audit,
                                     int *stageCounts,
                                     int *firstCx, int *firstSectionY,
                                     int *firstCz)
{
    int pending = 0;
    if (firstCx) *firstCx = 0;
    if (firstSectionY) *firstSectionY = 0;
    if (firstCz) *firstCz = 0;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dx = -1; dx <= 1; dx++) {
            for (int vertical = -1; vertical <= 1; vertical++) {
                int sectionY = audit->wait.focusSectionY + vertical;
                if (!SurfaceSectionInBounds(sectionY)) continue;
                ChunkSectionPipelineInfo info = { 0 };
                if (!ChunksGetSectionPipelineInfo(
                        audit->wait.focusCx + dx, sectionY,
                        audit->wait.focusCz + dz, &info)) {
                    info.stage = CHUNK_PIPELINE_MISSING_CHUNK;
                }
                if (stageCounts && info.stage >= CHUNK_PIPELINE_MISSING_CHUNK &&
                    info.stage <= CHUNK_PIPELINE_READY) {
                    stageCounts[info.stage]++;
                }
                if (!StreamWaitStageSettled(info.stage)) {
                    if (pending == 0) {
                        if (firstCx) *firstCx = audit->wait.focusCx + dx;
                        if (firstSectionY) *firstSectionY = sectionY;
                        if (firstCz) *firstCz = audit->wait.focusCz + dz;
                    }
                    pending++;
                }
            }
        }
    }
    return pending;
}

static void GameStreamWaitFrame(GameRuntime *game)
{
    if (!game || !game->streamAudit.wait.active) return;

    GameStreamAuditState *audit = &game->streamAudit;
    RequestStreamWaitSections(audit);
    ChunkStreamingStats stats = ChunksGetStreamingStats();
    int pendingGeneration = GetPendingGenJobCount();
    int pendingMesh = GetPendingMeshJobCount();
    int missingSurfaceChunks = PlayerMissingSurfaceChunkCount(
        game->player.position);
    int stageCounts[CHUNK_PIPELINE_READY + 1] = { 0 };
    int firstPendingCx = 0;
    int firstPendingSectionY = 0;
    int firstPendingCz = 0;
    int pendingLocalSections = PendingStreamWaitSections(
        audit, stageCounts, &firstPendingCx, &firstPendingSectionY,
        &firstPendingCz);
    bool settled = pendingLocalSections == 0;
    if (!AdvanceStreamWait(audit, settled)) return;
    bool complete = audit->wait.settledFrames >= 2u;

    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL stream wait result=%s elapsed_frames=%u "
        "focus=%d,%d,%d pending_local_sections=%d "
        "first_pending=%d,%d,%d "
        "pending_stages=missing:%d,gen_wait:%d,gen:%d,dirty:%d,mesh:%d "
        "pending_gen=%d pending_mesh=%d pending_mesh_snapshot_bytes=%llu "
        "missing_surface_chunks=%d\n",
        complete ? "settled" : "timeout", audit->wait.elapsedFrames,
        audit->wait.focusCx, audit->wait.focusSectionY,
        audit->wait.focusCz, pendingLocalSections,
        firstPendingCx, firstPendingSectionY, firstPendingCz,
        stageCounts[CHUNK_PIPELINE_MISSING_CHUNK],
        stageCounts[CHUNK_PIPELINE_GENERATION_WAIT],
        stageCounts[CHUNK_PIPELINE_GENERATION_QUEUED] +
            stageCounts[CHUNK_PIPELINE_GENERATION_RUNNING] +
            stageCounts[CHUNK_PIPELINE_GENERATION_DONE],
        stageCounts[CHUNK_PIPELINE_DIRTY_WAIT],
        stageCounts[CHUNK_PIPELINE_MESH_QUEUED] +
            stageCounts[CHUNK_PIPELINE_MESH_RUNNING] +
            stageCounts[CHUNK_PIPELINE_MESH_DONE],
        pendingGeneration, pendingMesh,
        (unsigned long long)stats.pendingMeshSnapshotBytes,
        missingSurfaceChunks);
    audit->wait.active = false;
    if (!complete) {
        game->debugStreamWaitFailed = true;
        snprintf(game->debugStreamWaitFailure,
                 sizeof(game->debugStreamWaitFailure),
                 "stream wait timed out after %u frames",
                 audit->wait.elapsedFrames);
    }
}

void GameStreamWaitStart(GameRuntime *game)
{
    if (!game || game->screen != SCREEN_PLAYING ||
        !WorldIsSurfaceActive()) {
        if (game) {
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL stream wait error "
                "reason=not_in_surface_world\n");
        }
        return;
    }
    if (game->streamAudit.wait.active) {
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL stream wait error reason=already_waiting\n");
        return;
    }
    if (game->streamAudit.active) {
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL stream wait error reason=audit_in_progress\n");
        return;
    }

    unsigned timeoutFrames = game->debugControl.streamWaitFrames;
    if (timeoutFrames < 1u ||
        timeoutFrames > DEBUG_CONTROL_STREAM_WAIT_MAX_FRAMES) {
        timeoutFrames = DEBUG_CONTROL_STREAM_WAIT_DEFAULT_FRAMES;
    }
    int blockX = (int)floorf(game->player.position.x);
    int blockZ = (int)floorf(game->player.position.z);
    int localX = 0;
    int localZ = 0;
    WorldToChunkLocal(blockX, blockZ,
                      &game->streamAudit.wait.focusCx,
                      &game->streamAudit.wait.focusCz,
                      &localX, &localZ);
    game->streamAudit.wait.focusSectionY = SurfaceSectionYFromBlockY(
        (int)floorf(game->player.position.y));
    game->streamAudit.wait.timeoutFrames = timeoutFrames;
    game->streamAudit.wait.elapsedFrames = 0u;
    game->streamAudit.wait.settledFrames = 0u;
    game->debugStreamWaitFailed = false;
    game->debugStreamWaitFailure[0] = '\0';
    game->streamAudit.wait.active = true;
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL stream wait started timeout_frames=%u\n",
        timeoutFrames);
}

void GameStreamAuditStart(GameRuntime *game)
{
    if (!game || game->screen != SCREEN_PLAYING ||
        !WorldIsSurfaceActive()) {
        if (game) {
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL stream audit error reason=not_in_surface_world\n");
        }
        return;
    }
    int radius = game->debugControl.streamAuditRadius;
    if (radius < 1 || radius > 4) radius = 2;
    int x = game->debugControl.streamAuditUsePlayerPosition
        ? (int)floorf(game->player.position.x)
        : game->debugControl.streamAuditX;
    int y = game->debugControl.streamAuditUsePlayerPosition
        ? (int)floorf(game->player.position.y)
        : game->debugControl.streamAuditY;
    int z = game->debugControl.streamAuditUsePlayerPosition
        ? (int)floorf(game->player.position.z)
        : game->debugControl.streamAuditZ;
    int localX = 0;
    int localZ = 0;
    GameStreamAuditState audit = {
        .focusSectionY = SurfaceSectionYFromBlockY(y),
        .radius = radius,
        .dx = -radius,
        .dz = -radius,
        .vertical = -1,
        .active = true
    };
    WorldToChunkLocal(x, z, &audit.focusCx, &audit.focusCz,
                      &localX, &localZ);
    game->streamAudit = audit;
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL stream audit started focus=%d,%d,%d radius=%d\n",
        audit.focusCx, audit.focusSectionY, audit.focusCz, radius);
}

void GameStreamAuditFrame(GameRuntime *game)
{
    if (!game) return;
    GameStreamWaitFrame(game);
    if (!game->streamAudit.active) return;
    GameStreamAuditState *audit = &game->streamAudit;
    audit->elapsedFrames++;
    if (audit->dz > audit->radius) {
        FinishAudit(game);
        return;
    }

    int cx = audit->focusCx + audit->dx;
    int cz = audit->focusCz + audit->dz;
    int sectionY = audit->focusSectionY + audit->vertical;
    const Chunk *chunk = FindChunk(cx, cz);
    if (audit->vertical == -1) {
        if (!chunk || !chunk->loaded) {
            audit->missingChunks++;
            GameStreamAuditSnapshot missing = SnapshotAt(
                cx, audit->focusSectionY, cz);
            AddSnapshot(audit, missing);
            AddIssue(audit, (GameStreamAuditIssue){
                .cx = cx,
                .cz = cz,
                .sectionY = audit->focusSectionY,
                .layer = GAME_STREAM_AUDIT_LAYER_CHUNK
            });
            audit->vertical = 1;
            AdvanceAudit(audit);
            return;
        }
        audit->loadedChunks++;
    }
    if (!SurfaceSectionInBounds(sectionY)) {
        AdvanceAudit(audit);
        return;
    }

    GameStreamAuditSnapshot snapshot = SnapshotAt(cx, sectionY, cz);
    AddSnapshot(audit, snapshot);
    GameStreamAuditExpected expected = CountExpectedLayers(
        cx, sectionY, cz);
    bool hasExpected = expected.solid > 0 || expected.water > 0 ||
                       expected.flora > 0;
    bool modeled = snapshot.solidVertices > 0 ||
                   snapshot.waterVertices > 0 ||
                   snapshot.floraVertices > 0;
    int issueCountBefore = audit->issuesTotal;
    AddLayerIssue(audit, &snapshot, GAME_STREAM_AUDIT_LAYER_SOLID,
                  expected.solid, snapshot.solidVertices);
    AddLayerIssue(audit, &snapshot, GAME_STREAM_AUDIT_LAYER_WATER,
                  expected.water, snapshot.waterVertices);
    AddLayerIssue(audit, &snapshot, GAME_STREAM_AUDIT_LAYER_FLORA,
                  expected.flora, snapshot.floraVertices);
    if (snapshot.resolved) audit->resolvedSections++;
    if (snapshot.materialized) audit->materializedSections++;
    if (!snapshot.materialized && hasExpected) audit->implicitOnlySections++;
    if (audit->issuesTotal > issueCountBefore) audit->unmeshedSections++;
    if (modeled) audit->modeledSections++;
    if (snapshot.dirty) audit->dirtySections++;
    AdvanceAudit(audit);
}

#ifdef GAME_STREAM_AUDIT_TESTING
void GameStreamAuditCountExpectedForTest(int cx, int sectionY, int cz,
                                         int *solid, int *water, int *flora)
{
    GameStreamAuditExpected expected = CountExpectedLayers(cx, sectionY, cz);
    if (solid) *solid = expected.solid;
    if (water) *water = expected.water;
    if (flora) *flora = expected.flora;
}

bool GameStreamAuditSnapshotsEqualForTest(
    const GameStreamAuditSnapshot *left,
    const GameStreamAuditSnapshot *right)
{
    return left && right && SnapshotEqual(left, right);
}

bool GameStreamAuditLayerMissingForTest(int expected, int vertices)
{
    return LayerMissing(expected, vertices);
}

void GameStreamAuditAdvanceForTest(GameStreamAuditState *audit)
{
    if (audit) AdvanceAudit(audit);
}

bool GameStreamWaitStageSettledForTest(int stage)
{
    return StreamWaitStageSettled((ChunkPipelineStage)stage);
}

bool GameStreamWaitAdvanceForTest(GameStreamAuditState *audit, bool settled)
{
    return audit && AdvanceStreamWait(audit, settled);
}

int GameStreamWaitRequestSectionsForTest(const GameStreamAuditState *audit)
{
    return audit ? RequestStreamWaitSections(audit) : 0;
}
#endif
