#include "app/game_debug_volcano_history.h"
#include "world/volcano_history.h"
#include "world/volcano_history_cache.h"
#include "world/terrain_generation.h"
#include "world/world_surface_snapshot.h"
#include "world/terrain.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double InitialGround(Vector3 direction, void *user)
{
    const WorldSurfaceSnapshot *s = user;
    return TerrainPlanetBaselineAtGeographic(s->context.seed, &s->planetProfile,
        atan2f(direction.z, direction.x), asinf(fmaxf(-1, fminf(1, direction.y))), NULL) + 1;
}
bool GameDebugVolcanoHistory(DebugControl *c)
{
    c->historyReady = false;
    WorldSurfaceSnapshot s = *WorldCurrentSurfaceSnapshot();
    if (!c->geologyTerrainFound || s.context.kind != WORLD_SURFACE_PLANET ||
        s.context.bodyId != c->geologyTerrainBody || s.context.seed != c->geologyTerrainSeed) return false;
    VolcanoTerrainDefinition d;
    if (!VolcanoTerrainDefine(s.context.seed, s.planetProfile.volcanicActivity,
        (SurfaceAddress){s.context.bodyId, c->geologyTerrainFace, c->geologyTerrainU, c->geologyTerrainV, 0}, &d)) return false;
    VolcanoHistoryConfig config = VolcanoHistoryDefaults(); config.cellSize = c->historyCellSize;
    clock_t start = clock();
    VolcanoHistory *h = VolcanoHistoryBuild(&d, config, InitialGround, &s);
    if (!h) return false;
    VolcanoHistoryStats stats = VolcanoHistoryInspect(h);
    double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
    FILE *file = fopen("volcano-history.csv", "w");
    bool ok = file != NULL;
    if (file) {
        fprintf(file, "event,x,y,ground,depth,flow_mask,rock\n");
        for (unsigned event = 0; event < stats.events; event++) {
            VolcanoHistoryEvent e; VolcanoHistoryEventAt(h, event, &e);
            DebugControlReply(c, "DEBUG_CONTROL geology history_event event=%u source=%u,%u requested=%.9g accepted=%.9g temperature=%.9g terminal_cooling=%.9g\n",
                event, e.sourceX, e.sourceY, e.requestedVolume, e.acceptedVolume, e.temperatureK, e.terminalCooling);
            for (unsigned y = 0; y < stats.side; y++) for (unsigned x = 0; x < stats.side; x++) {
                VolcanoHistoryCell cell; VolcanoHistoryRead(h, event, x, y, &cell);
                fprintf(file, "%u,%g,%g,%.12g,%.12g,%u,%s\n", event,
                    ((double)x - stats.side/2) * config.cellSize,
                    ((double)y - stats.side/2) * config.cellSize,
                    cell.ground, cell.depth, cell.flowMask, LavaRockName(cell.rock));
            }
        }
        ok = !ferror(file);
        if (fclose(file)) ok = false;
    }
    c->historyReady = ok;
    c->historySupplied = stats.ledger.suppliedVolume; c->historyRemaining = stats.remainingVolume;
    c->historyFlank = stats.flankVolume; c->historyFoot = stats.footVolume;
    c->historyBoundary = stats.ledger.boundaryVolume; c->historyRadius = stats.flowRadius;
    c->historyVolumeError = stats.ledger.volumeError; c->historyEnergyError = stats.ledger.energyError;
    c->historyCooling = stats.terminalCooling;
    DebugControlReply(c, "DEBUG_CONTROL geology history ok=%d cell=%u side=%u supplied=%.9g remaining=%.9g flank=%.9g foot=%.9g radius=%.9g boundary=%.9g volume_error=%.12g energy_error=%.12g seconds=%.6f peak_bytes=%zu export=volcano-history.csv\n",
        ok, config.cellSize, stats.side, c->historySupplied, c->historyRemaining, c->historyFlank, c->historyFoot,
        c->historyRadius, c->historyBoundary, c->historyVolumeError, c->historyEnergyError, seconds, stats.peakBytes);
    VolcanoHistoryDestroy(h); return ok;
}
bool GameDebugVolcanoHistoryWorld(DebugControl *c)
{
    c->historyWorldReady = false;
    const WorldSurfaceSnapshot *s = WorldCurrentSurfaceSnapshot();
    if (!c->geologyTerrainFound || s->context.kind != WORLD_SURFACE_PLANET ||
        s->context.seed != c->geologyTerrainSeed || s->context.bodyId != c->geologyTerrainBody ||
        TerrainGenerationVersion() < TERRAIN_GENERATION_VOLCANO_HISTORY) return false;
    SurfaceAddress center = {0, c->geologyTerrainFace, c->geologyTerrainU, c->geologyTerrainV, 0};
    VolcanoHistoricalTerrain h = VolcanoHistoricalTerrainAt(s->context.seed, &s->planetProfile,
        SurfaceAddressDirection(center));
    if (!h.domain) return false;
    c->historyWorldReady = !h.boundaryRejected;
    c->historyWorldDepth = h.depth; c->historyRepresented = h.representedVolume;
    c->historyQuantizationLoss = h.quantizationLoss; c->historyRejected = h.rejectedVolume;
    VolcanoHistoryCacheStats cache = VolcanoHistoryCacheInspect();
    DebugControlReply(c, "DEBUG_CONTROL geology history_world ready=%d generation=%u depth=%u material=%s fit_attempts=%u represented=%.9g quantization_loss=%.9g rejected=%.9g retained=%.9g remaining=%.9g hits=%llu builds=%llu evictions=%llu residents=%u resident_bytes=%zu peak_bytes=%zu build_seconds=%.6f\n",
        c->historyWorldReady, TerrainGenerationVersion(), h.depth, LavaRockName(h.rock), h.fitAttempts,
        h.representedVolume, h.quantizationLoss, h.rejectedVolume, h.model.ledger.retainedVolume,
        h.model.remainingVolume, (unsigned long long)cache.hits, (unsigned long long)cache.builds,
        (unsigned long long)cache.evictions, cache.residents, cache.residentBytes, cache.peakBytes, cache.buildSeconds);
    return true;
}
bool GameDebugVolcanoHistoryResolve(const DebugControl *c, const char *field, DebugDslValue *out)
{
    if (!strcmp(field, "history_world_ready")) {
        out->type = DEBUG_DSL_VALUE_BOOL; out->as.boolean = c->historyWorldReady; return true;
    }
    out->type = DEBUG_DSL_VALUE_NUMBER;
    if (!strcmp(field, "history_world_depth")) { out->as.number = c->historyWorldDepth; return true; }
    if (!strcmp(field, "history_represented")) { out->as.number = c->historyRepresented; return true; }
    if (!strcmp(field, "history_quantization_loss")) { out->as.number = c->historyQuantizationLoss; return true; }
    if (!strcmp(field, "history_rejected")) { out->as.number = c->historyRejected; return true; }
    if (!strcmp(field, "history_ready")) {
        out->type = DEBUG_DSL_VALUE_BOOL; out->as.boolean = c->historyReady; return true;
    }
    out->type = DEBUG_DSL_VALUE_NUMBER;
    if (!strcmp(field, "history_supplied")) out->as.number = c->historySupplied;
    else if (!strcmp(field, "history_remaining")) out->as.number = c->historyRemaining;
    else if (!strcmp(field, "history_flank")) out->as.number = c->historyFlank;
    else if (!strcmp(field, "history_foot")) out->as.number = c->historyFoot;
    else if (!strcmp(field, "history_boundary")) out->as.number = c->historyBoundary;
    else if (!strcmp(field, "history_radius")) out->as.number = c->historyRadius;
    else if (!strcmp(field, "history_volume_error")) out->as.number = c->historyVolumeError;
    else if (!strcmp(field, "history_energy_error")) out->as.number = c->historyEnergyError;
    else if (!strcmp(field, "history_terminal_cooling")) out->as.number = c->historyCooling;
    else return false;
    if (!c->historyReady) out->as.number = 0;
    return true;
}
