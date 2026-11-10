#ifndef VOXELCRAFT_GAME_COVERAGE_EVIDENCE_H
#define VOXELCRAFT_GAME_COVERAGE_EVIDENCE_H

#include "core/debug_dsl.h"
#include "world/surface_coverage.h"

#include <stdbool.h>

struct GameRuntime;

/* Frozen diagnostic snapshot for physical coverage. `coverage.ready` is terrain
 * readiness; `coverage.meshReady` and `coverage.visualReady` are separate visual
 * evidence and never imply requested/admitted columns were drawn. */
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
