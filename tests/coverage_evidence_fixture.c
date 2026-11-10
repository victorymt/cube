#include "coverage_evidence_fixture.h"

#include "app/game_coverage_evidence.h"

#include <assert.h>

static void AssertNumber(const GameCoverageEvidence *evidence,
                         const char *name, double expected)
{
    DebugDslValue value = { 0 };
    assert(GameCoverageEvidenceDslResolve(name, evidence, &value));
    assert(value.type == DEBUG_DSL_VALUE_NUMBER);
    assert(value.as.number == expected);
}

static void AssertBool(const GameCoverageEvidence *evidence,
                       const char *name, bool expected)
{
    DebugDslValue value = { 0 };
    assert(GameCoverageEvidenceDslResolve(name, evidence, &value));
    assert(value.type == DEBUG_DSL_VALUE_BOOL);
    assert(value.as.boolean == expected);
}

/* One fixture pins the complete M5 evidence schema and its conservative local
 * visual-readiness rule. It does not claim full viewport or draw coverage. */
void CoverageEvidenceFixtureAssert(void)
{
    SurfaceCoverageEvidence coverage = SurfaceCoverageEvidenceBuild(
        800u, 625u, 625u, 0u, 0u, 7u, 3u, false,
        4u, 2u, 625u, true);
    GameCoverageEvidence evidence = {
        .coverage = coverage,
        .active = true,
        .available = true
    };
    assert(coverage.localTerrainReady);
    assert(coverage.visualReady);

    AssertBool(&evidence, "stream.cube.active", true);
    AssertBool(&evidence, "stream.cube.available", true);
    AssertBool(&evidence, "stream.cube.saturated", false);
    AssertBool(&evidence, "stream.cube.local_terrain_ready", true);
    AssertBool(&evidence, "stream.cube.visual_ready", true);
    AssertNumber(&evidence, "stream.cube.requested", 800.0);
    AssertNumber(&evidence, "stream.cube.admitted", 625.0);
    AssertNumber(&evidence, "stream.cube.ready", 625.0);
    AssertNumber(&evidence, "stream.cube.pending", 0.0);
    AssertNumber(&evidence, "stream.cube.missing", 0.0);
    AssertNumber(&evidence, "stream.cube.submitted", 7.0);
    AssertNumber(&evidence, "stream.cube.released", 3.0);
    AssertNumber(&evidence, "stream.cube.duplicates", 4.0);
    AssertNumber(&evidence, "stream.cube.stale", 2.0);
    AssertNumber(&evidence, "stream.cube.mesh_ready", 625.0);
    DebugDslValue unknown = { 0 };
    assert(!GameCoverageEvidenceDslResolve(
        "stream.cube.drawn", &evidence, &unknown));

    coverage = SurfaceCoverageEvidenceBuild(
        625u, 625u, 625u, 0u, 0u, 0u, 0u, false, 0u, 0u, 624u, true);
    assert(!coverage.visualReady);
    coverage = SurfaceCoverageEvidenceBuild(
        625u, 625u, 625u, 0u, 0u, 0u, 0u, false, 0u, 0u, 625u, false);
    assert(!coverage.visualReady);
    coverage = SurfaceCoverageEvidenceBuild(
        625u, 625u, 624u, 1u, 0u, 0u, 0u, false, 0u, 0u, 625u, true);
    assert(!coverage.visualReady);
    coverage = SurfaceCoverageEvidenceBuild(
        625u, 625u, 625u, 0u, 0u, 0u, 0u, true, 0u, 0u, 625u, true);
    assert(!coverage.visualReady);
}
