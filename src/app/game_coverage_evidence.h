#ifndef VOXELCRAFT_GAME_COVERAGE_EVIDENCE_H
#define VOXELCRAFT_GAME_COVERAGE_EVIDENCE_H

#include "core/debug_dsl.h"
#include "world/surface_coverage.h"

#include <stdbool.h>

struct GameRuntime;

/* Frozen diagnostic snapshot for physical coverage. `coverage.ready` is terrain
 * readiness; `coverage.targetLodReady` is section-level target-LOD readiness.
 * `coverage.visualReady` never implies requested/admitted columns were drawn. */
typedef struct GameCoverageEvidence {
    SurfaceCoverageEvidence coverage;
    bool active;
    bool available;
} GameCoverageEvidence;

bool GameCoverageEvidenceCapture(const struct GameRuntime *game,
                                 GameCoverageEvidence *out);
bool GameCoverageEvidenceDslResolve(const char *name,
                                    const GameCoverageEvidence *evidence,
                                    DebugDslValue *outValue);

#endif
