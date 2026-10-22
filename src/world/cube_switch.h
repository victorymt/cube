#ifndef VOXELCRAFT_CUBE_SWITCH_H
#define VOXELCRAFT_CUBE_SWITCH_H

#include <stdbool.h>

/*
 * Staged activation of a cube-grid surface world.
 *
 * Switching grids is not a coordinate change: the two grids give different
 * identities to the same position, so a switch must rebuild the world rather
 * than reinterpret it. This state machine sequences that rebuild and refuses
 * to publish anything until the final step succeeds. Until then the previous
 * world remains the committed one.
 *
 * The steps are expressed as values, not callbacks into the world layer, so
 * the ordering and rollback rules can be tested without a live game.
 */

typedef enum CubeSwitchStage {
    CUBE_SWITCH_IDLE = 0,
    /* Workers drained and the old surface released. */
    CUBE_SWITCH_DRAINED,
    /* A new generation context exists for the requested body. */
    CUBE_SWITCH_CONTEXT_READY,
    /* Dependent state (edits, undo, fluids, region runtimes) cleared. */
    CUBE_SWITCH_RESET,
    /* A valid standing pose was found on generated terrain. */
    CUBE_SWITCH_LANDED,
    /* Minimum coverage reached; the world may be published. */
    CUBE_SWITCH_READY,
    /* Terminal: the new world is committed. */
    CUBE_SWITCH_COMMITTED,
    /* Terminal: nothing was published and the old world still applies. */
    CUBE_SWITCH_ROLLED_BACK
} CubeSwitchStage;

typedef struct CubeSwitchProgress {
    CubeSwitchStage stage;
    /* Attempts spent waiting for coverage; bounded so a stall cannot hang. */
    unsigned coverageAttempts;
    /* Non-zero when the machine stopped early. */
    unsigned failureStep;
} CubeSwitchProgress;

/* Requests a switch. Fails unless the runtime gate is open. */
bool CubeSwitchBegin(CubeSwitchProgress *progress);
/* True when the gate is open, i.e. a cube switch is permitted at all. */
bool CubeSwitchPermitted(void);
/* Advances one step. Returns false when the machine is already terminal.
 * CUBE_SWITCH_READY is a wait state: callers report actual coverage with
 * CubeSwitchReportCoverage rather than advancing past it blindly. */
bool CubeSwitchAdvance(CubeSwitchProgress *progress);
/* Records one coverage observation while in READY. `enough` commits the
 * switch; repeated shortfalls eventually roll back instead of hanging. */
bool CubeSwitchReportCoverage(CubeSwitchProgress *progress, bool enough);
/* Any stage before CONTEXT_READY may be abandoned without cleanup. */
bool CubeSwitchAbort(CubeSwitchProgress *progress);
bool CubeSwitchIsTerminal(const CubeSwitchProgress *progress);
bool CubeSwitchIsCommitted(const CubeSwitchProgress *progress);

#endif
