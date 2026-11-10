/* Coverage evidence must preserve every declared counter and keep local terrain
 * readiness, mesh presence, and visual readiness independent. This covers the
 * synthetic capture boundary, not GPU draw or full-viewport coverage. */
#include "app/game_coverage_evidence.h"
#include "app/game_runtime.h"
#include "coverage_evidence_fixture.h"
#include "world/chunks_cube.h"

#include <assert.h>
#include <stdio.h>

static bool cubeActive;
static bool cubeHasCoverage;
static CubeCoverageStatus cubeStatus;
static CubeCoverageObservation cubeObservation;
static unsigned cubeTargetLodReady;
static unsigned cubeTargetLodMissingSections;
static int missingSurfaceChunks;

bool ChunksCubeCoverageIsActive(void)
{
    return cubeActive;
}

bool ChunksLastCubeCoverage(CubeCoverageStatus *outStatus)
{
    if (!cubeHasCoverage || !outStatus) return false;
    *outStatus = cubeStatus;
    return true;
}

unsigned ChunksCubeTargetLodReadyCount(void)
{
    return cubeTargetLodReady;
}

unsigned ChunksCubeTargetLodMissingSections(void)
{
    return cubeTargetLodMissingSections;
}

bool ChunksCubeCoverageObservation(CubeCoverageObservation *outObservation)
{
    if (!cubeHasCoverage || !outObservation) return false;
    *outObservation = cubeObservation;
    return true;
}

int PlayerMissingSurfaceChunkCount(Vector3 position)
{
    (void)position;
    return missingSurfaceChunks;
}

bool WorldIsSurfaceActive(void)
{
    return true;
}

static void ResetCapture(void)
{
    cubeActive = true;
    cubeHasCoverage = true;
    cubeStatus = (CubeCoverageStatus){
        .requested = 650u,
        .admitted = 625u,
        .ready = 625u,
        .pending = 0u,
        .missing = 0u,
        .submitted = 4u,
        .released = 2u,
        .duplicates = 5u,
        .stale = 1u,
        .saturated = false
    };
    cubeObservation = (CubeCoverageObservation){
        .sequence = 77u,
        .bodyId = 0u,
        .requested = 650u,
        .admitted = 625u,
        .saturated = false
    };
    cubeTargetLodReady = 625u;
    cubeTargetLodMissingSections = 0u;
    missingSurfaceChunks = 0;
}

static void TestCaptureSeparatesReadiness(void)
{
    GameRuntime game = { 0 };
    game.screen = SCREEN_PLAYING;
    GameCoverageEvidence evidence;

    ResetCapture();
    assert(GameCoverageEvidenceCapture(&game, &evidence));
    assert(evidence.active && evidence.available);
    assert(evidence.coverage.localTerrainReady);
    assert(evidence.coverage.visualReady);
    assert(evidence.coverage.duplicates == 5u);
    assert(evidence.coverage.stale == 1u);
    assert(evidence.coverage.targetLodReady == 625u);
    assert(evidence.coverage.targetLodMissingSections == 0u);
    assert(evidence.coverage.observationSequence == 77u);

    missingSurfaceChunks = 1;
    assert(GameCoverageEvidenceCapture(&game, &evidence));
    assert(!evidence.coverage.localTerrainReady);
    assert(!evidence.coverage.visualReady);

    ResetCapture();
    cubeTargetLodReady = 624u;
    cubeTargetLodMissingSections = 1u;
    assert(GameCoverageEvidenceCapture(&game, &evidence));
    assert(evidence.coverage.localTerrainReady);
    assert(!evidence.coverage.visualReady);

    ResetCapture();
    cubeStatus.saturated = true;
    assert(GameCoverageEvidenceCapture(&game, &evidence));
    assert(evidence.coverage.localTerrainReady);
    assert(!evidence.coverage.visualReady);

    ResetCapture();
    cubeActive = false;
    cubeHasCoverage = false;
    assert(GameCoverageEvidenceCapture(&game, &evidence));
    assert(!evidence.active && !evidence.available);
    assert(evidence.coverage.requested == 0u);
    assert(!evidence.coverage.localTerrainReady);
    assert(!evidence.coverage.visualReady);
}

int main(void)
{
    CoverageEvidenceFixtureAssert();
    TestCaptureSeparatesReadiness();
    puts("coverage evidence tests passed");
    return 0;
}
