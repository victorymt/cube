#ifndef VOXELCRAFT_GAME_DEBUG_TRACE_H
#define VOXELCRAFT_GAME_DEBUG_TRACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct GameDebugTraceState {
    FILE *file;
    double elapsed;
    double nextSample;
    double startedMonotonicMs;
    double startedCpuMs;
    double startedMainCpuMs;
    int64_t startedUnixMs;
    uint64_t frame;
    int lastCx;
    int lastCz;
    int lastSectionY;
    bool haveFocus;
    bool lastInvisibleTarget;
    bool lastFeetSubmerged;
    bool lastBodySubmerged;
    bool lastEyesSubmerged;
    bool ioErrorReported;
} GameDebugTraceState;

struct GameFrameView;
struct GameRuntime;

bool GameDebugTraceSetPath(char *destination, size_t destinationSize,
                           const char *source);
bool GameDebugTraceStart(struct GameRuntime *game);
double GameDebugTraceMainCpuNowMs(void);
void GameDebugTraceFrame(struct GameRuntime *game,
                         const struct GameFrameView *frame);
void GameDebugTraceEvent(struct GameRuntime *game, const char *reason);
void GameDebugTraceStop(struct GameRuntime *game);

#ifdef GAME_DEBUG_TRACE_TESTING
bool GameDebugTraceEscapeForTest(const char *value, char *destination,
                                 size_t destinationSize);
double GameDebugTraceAdvanceDeadlineForTest(double deadline, double elapsed);
bool GameDebugTraceWriteEventForTest(struct GameRuntime *game,
                                     const char *reason);
#endif

#endif
