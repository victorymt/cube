#ifndef VOXELCRAFT_GAME_STREAM_AUDIT_H
#define VOXELCRAFT_GAME_STREAM_AUDIT_H

#include <stdbool.h>
#include <stdint.h>

#define GAME_STREAM_AUDIT_MAX_ISSUES 16
#define GAME_STREAM_AUDIT_MAX_SNAPSHOTS 243

typedef enum GameStreamAuditLayer {
    GAME_STREAM_AUDIT_LAYER_CHUNK = 0,
    GAME_STREAM_AUDIT_LAYER_SOLID,
    GAME_STREAM_AUDIT_LAYER_WATER,
    GAME_STREAM_AUDIT_LAYER_FLORA
} GameStreamAuditLayer;

typedef struct GameStreamAuditIssue {
    int cx;
    int cz;
    int sectionY;
    int expected;
    int vertices;
    uint32_t dirtyStamp;
    GameStreamAuditLayer layer;
    bool loaded;
    bool resolved;
    bool materialized;
    bool dirty;
} GameStreamAuditIssue;

typedef struct GameStreamAuditSnapshot {
    int cx;
    int cz;
    int sectionY;
    uint32_t generation;
    uint32_t dirtyStamp;
    int solidVertices;
    int waterVertices;
    int floraVertices;
    bool loaded;
    bool resolved;
    bool materialized;
    bool dirty;
} GameStreamAuditSnapshot;

typedef struct GameStreamAuditState {
    GameStreamAuditIssue issues[GAME_STREAM_AUDIT_MAX_ISSUES];
    GameStreamAuditSnapshot snapshots[GAME_STREAM_AUDIT_MAX_SNAPSHOTS];
    int focusCx;
    int focusCz;
    int focusSectionY;
    int radius;
    int dx;
    int dz;
    int vertical;
    int loadedChunks;
    int missingChunks;
    int resolvedSections;
    int materializedSections;
    int implicitOnlySections;
    int unmeshedSections;
    int modeledSections;
    int dirtySections;
    int issuesTotal;
    int issuesEmitted;
    int snapshotCount;
    int elapsedFrames;
    int staleSections;
    bool active;
    struct {
        int focusCx;
        int focusCz;
        int focusSectionY;
        unsigned timeoutFrames;
        unsigned elapsedFrames;
        unsigned settledFrames;
        bool active;
    } wait;
} GameStreamAuditState;

struct GameRuntime;

void GameStreamAuditStart(struct GameRuntime *game);
void GameStreamAuditFrame(struct GameRuntime *game);
void GameStreamWaitStart(struct GameRuntime *game);

#ifdef GAME_STREAM_AUDIT_TESTING
void GameStreamAuditCountExpectedForTest(int cx, int sectionY, int cz,
                                         int *solid, int *water, int *flora);
bool GameStreamAuditSnapshotsEqualForTest(
    const GameStreamAuditSnapshot *left,
    const GameStreamAuditSnapshot *right);
bool GameStreamAuditLayerMissingForTest(int expected, int vertices);
void GameStreamAuditAdvanceForTest(GameStreamAuditState *audit);
bool GameStreamWaitStageSettledForTest(int stage);
bool GameStreamWaitAdvanceForTest(GameStreamAuditState *audit, bool settled);
int GameStreamWaitRequestSectionsForTest(const GameStreamAuditState *audit);
#endif

#endif
