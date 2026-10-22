/* Staged cube activation: ordering, coverage wait, rollback and the gate. */
#include "world/cube_switch.h"
#include "world/world_surface_snapshot.h"

#include <assert.h>
#include <stdio.h>

static void TestGateClosedRefuses(void)
{
    WorldSetCubeRuntimeEnabled(false);
    CubeSwitchProgress progress;
    assert(!CubeSwitchBegin(&progress));
    assert(CubeSwitchIsTerminal(&progress));
    assert(!CubeSwitchIsCommitted(&progress));
    assert(progress.failureStep == 1u);
}

static void TestHappyPathCommits(void)
{
    WorldSetCubeRuntimeEnabled(true);
    CubeSwitchProgress progress;
    assert(CubeSwitchBegin(&progress));
    assert(progress.stage == CUBE_SWITCH_IDLE);

    /* Steps advance in the documented order until coverage is awaited. */
    assert(CubeSwitchAdvance(&progress) && progress.stage == CUBE_SWITCH_DRAINED);
    assert(CubeSwitchAdvance(&progress) && progress.stage == CUBE_SWITCH_CONTEXT_READY);
    assert(CubeSwitchAdvance(&progress) && progress.stage == CUBE_SWITCH_RESET);
    assert(CubeSwitchAdvance(&progress) && progress.stage == CUBE_SWITCH_LANDED);
    assert(CubeSwitchAdvance(&progress) && progress.stage == CUBE_SWITCH_READY);

    /* Nothing is published while waiting. */
    assert(!CubeSwitchIsTerminal(&progress));
    assert(CubeSwitchReportCoverage(&progress, false));
    assert(!CubeSwitchIsCommitted(&progress));
    assert(CubeSwitchReportCoverage(&progress, true));
    assert(CubeSwitchIsCommitted(&progress));
    assert(CubeSwitchIsTerminal(&progress));
    WorldSetCubeRuntimeEnabled(false);
}

static void TestCoverageStallRollsBack(void)
{
    WorldSetCubeRuntimeEnabled(true);
    CubeSwitchProgress progress;
    assert(CubeSwitchBegin(&progress));
    while (progress.stage != CUBE_SWITCH_READY) assert(CubeSwitchAdvance(&progress));

    /* Repeated shortfalls must give up rather than hang forever. */
    bool stillGoing = true;
    for (int i = 0; i < 16 && stillGoing; i++)
        stillGoing = CubeSwitchReportCoverage(&progress, false);
    assert(!stillGoing);
    assert(progress.stage == CUBE_SWITCH_ROLLED_BACK);
    assert(progress.failureStep == 5u);
    assert(!CubeSwitchIsCommitted(&progress));
    WorldSetCubeRuntimeEnabled(false);
}

static void TestAbortBeforeCommit(void)
{
    WorldSetCubeRuntimeEnabled(true);
    CubeSwitchProgress progress;
    assert(CubeSwitchBegin(&progress));
    assert(CubeSwitchAdvance(&progress));
    assert(CubeSwitchAbort(&progress));
    assert(progress.stage == CUBE_SWITCH_ROLLED_BACK);
    assert(!CubeSwitchIsCommitted(&progress));
    /* An aborted switch cannot be revived by advancing. */
    assert(!CubeSwitchAdvance(&progress));
    WorldSetCubeRuntimeEnabled(false);
}

static void TestTerminalIsFinal(void)
{
    WorldSetCubeRuntimeEnabled(true);
    CubeSwitchProgress progress;
    assert(CubeSwitchBegin(&progress));
    while (!CubeSwitchIsTerminal(&progress)) {
        if (progress.stage == CUBE_SWITCH_READY)
            CubeSwitchReportCoverage(&progress, true);
        else
            CubeSwitchAdvance(&progress);
    }
    assert(CubeSwitchIsCommitted(&progress));
    assert(!CubeSwitchAdvance(&progress));
    assert(!CubeSwitchReportCoverage(&progress, true));
    assert(!CubeSwitchAbort(&progress));
    WorldSetCubeRuntimeEnabled(false);
}

static void TestInvalidInput(void)
{
    assert(!CubeSwitchBegin(NULL));
    assert(!CubeSwitchAdvance(NULL));
    assert(!CubeSwitchAbort(NULL));
    assert(!CubeSwitchReportCoverage(NULL, true));
    assert(!CubeSwitchIsTerminal(NULL));
    assert(!CubeSwitchIsCommitted(NULL));
}

int main(void)
{
    TestGateClosedRefuses();
    TestHappyPathCommits();
    TestCoverageStallRollsBack();
    TestAbortBeforeCommit();
    TestTerminalIsFinal();
    TestInvalidInput();
    puts("cube switch state machine tests passed");
    return 0;
}
