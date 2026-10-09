#ifndef VOXELCRAFT_VOLCANO_HISTORY_H
#define VOXELCRAFT_VOLCANO_HISTORY_H

#include "world/volcano_terrain.h"
#include "world/volcano_deposition_model.h"
#include "world/lava_model.h"

#define VOLCANO_HISTORY_VERSION 1u
#define VOLCANO_HISTORY_MAX_EVENTS 4u

/* A bounded, world-independent generator. The callback supplies INITIAL ground
   only: it must never query a history-dependent height. NULL samples the pure
   mountain relief. Input/configuration are copied; no callback is retained. */
typedef double (*VolcanoHistoryGround)(Vector3 direction, void *user);
typedef struct VolcanoHistoryConfig {
    unsigned cellSize; /* 2, 4 or 8 blocks; same physical domain at all resolutions. */
    unsigned events, supplySteps, settleSteps;
    double volumeScale, cooling, mobility, maxDepth; /* maxDepth per eruption */
} VolcanoHistoryConfig;
typedef struct VolcanoHistoryEvent {
    unsigned sourceX, sourceY;
    double requestedVolume, acceptedVolume, temperatureK, terminalCooling;
    VolcanoDepositionLedger ledger; /* cumulative, AFTER explicit era cooling */
} VolcanoHistoryEvent;
typedef struct VolcanoHistoryCell {
    double ground, depth;
    unsigned flowMask; /* one bit per eruption that supplied/flowed through column */
    LavaRock rock;
} VolcanoHistoryCell;
typedef struct VolcanoHistoryStats {
    unsigned side, events;
    double cellSize, extent, requestedVolume, remainingVolume, terminalCooling;
    double flankVolume, footVolume, maxDepth, flowRadius;
    size_t peakBytes;
    VolcanoDepositionLedger ledger;
} VolcanoHistoryStats;
typedef struct VolcanoHistory VolcanoHistory;

VolcanoHistoryConfig VolcanoHistoryDefaults(void);
VolcanoHistory *VolcanoHistoryBuild(const VolcanoTerrainDefinition *definition,
    VolcanoHistoryConfig config, VolcanoHistoryGround ground, void *user);
void VolcanoHistoryDestroy(VolcanoHistory *history);
VolcanoHistoryStats VolcanoHistoryInspect(const VolcanoHistory *history);
bool VolcanoHistoryRead(const VolcanoHistory *history, unsigned event,
    unsigned x, unsigned y, VolcanoHistoryCell *out);
bool VolcanoHistoryEventAt(const VolcanoHistory *history, unsigned event,
    VolcanoHistoryEvent *out);
/* Tangent basis is anchored at the stable center, including near poles/faces. */
Vector3 VolcanoHistoryDirection(const VolcanoHistory *history, unsigned x, unsigned y);

#endif
