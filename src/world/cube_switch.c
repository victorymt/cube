#include "world/cube_switch.h"
#include "world/world_surface_snapshot.h"

#define CUBE_SWITCH_MAX_COVERAGE_ATTEMPTS 8u

bool CubeSwitchPermitted(void)
{
    return WorldCubeRuntimeEnabled();
}

bool CubeSwitchBegin(CubeSwitchProgress *progress)
{
    if (!progress) return false;
    /* The gate keeps the default runtime on the legacy grid. A caller that has
     * not opened it is asking for a world the rest of the game cannot serve. */
    if (!WorldCubeRuntimeEnabled()) {
        *progress = (CubeSwitchProgress){
            .stage = CUBE_SWITCH_ROLLED_BACK,
            .coverageAttempts = 0u,
            .failureStep = 1u
        };
        return false;
    }
    *progress = (CubeSwitchProgress){ .stage = CUBE_SWITCH_IDLE };
    return true;
}

bool CubeSwitchIsTerminal(const CubeSwitchProgress *progress)
{
    return progress && (progress->stage == CUBE_SWITCH_COMMITTED ||
                        progress->stage == CUBE_SWITCH_ROLLED_BACK);
}

bool CubeSwitchIsCommitted(const CubeSwitchProgress *progress)
{
    return progress && progress->stage == CUBE_SWITCH_COMMITTED;
}

bool CubeSwitchAdvance(CubeSwitchProgress *progress)
{
    if (!progress || CubeSwitchIsTerminal(progress)) return false;
    switch (progress->stage) {
    case CUBE_SWITCH_IDLE:
        progress->stage = CUBE_SWITCH_DRAINED;
        return true;
    case CUBE_SWITCH_DRAINED:
        progress->stage = CUBE_SWITCH_CONTEXT_READY;
        return true;
    case CUBE_SWITCH_CONTEXT_READY:
        progress->stage = CUBE_SWITCH_RESET;
        return true;
    case CUBE_SWITCH_RESET:
        progress->stage = CUBE_SWITCH_LANDED;
        return true;
    case CUBE_SWITCH_LANDED:
        /* Landing does not imply coverage: the first frames legitimately have
         * pending columns. Enter the wait explicitly so callers can see the
         * state and bound the wait. */
        progress->stage = CUBE_SWITCH_READY;
        progress->coverageAttempts = 0u;
        return true;
    case CUBE_SWITCH_READY:
        /* Waiting for coverage; Advance only counts the pass. */
        progress->coverageAttempts++;
        return progress->coverageAttempts <= CUBE_SWITCH_MAX_COVERAGE_ATTEMPTS;
    default:
        return false;
    }
}

bool CubeSwitchAbort(CubeSwitchProgress *progress)
{
    if (!progress || CubeSwitchIsTerminal(progress)) return false;
    progress->stage = CUBE_SWITCH_ROLLED_BACK;
    progress->failureStep = 0u;
    return true;
}

bool CubeSwitchReportCoverage(CubeSwitchProgress *progress, bool enough)
{
    if (!progress || progress->stage != CUBE_SWITCH_READY) return false;
    if (enough) {
        progress->stage = CUBE_SWITCH_COMMITTED;
        return true;
    }
    progress->coverageAttempts++;
    if (progress->coverageAttempts < CUBE_SWITCH_MAX_COVERAGE_ATTEMPTS)
        return true;
    progress->stage = CUBE_SWITCH_ROLLED_BACK;
    progress->failureStep = 5u;
    return false;
}
