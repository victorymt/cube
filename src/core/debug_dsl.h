#ifndef VOXELCRAFT_DEBUG_DSL_H
#define VOXELCRAFT_DEBUG_DSL_H

#include <stdbool.h>
#include <stddef.h>

#define DEBUG_DSL_MAX_REPEAT_COUNT 3600u
#define DEBUG_DSL_MAX_REPEAT_DEPTH 32u
#define DEBUG_DSL_MAX_EXPRESSION_DEPTH 64u
#define DEBUG_DSL_MAX_COMMAND_TEXT 1024u
#define DEBUG_DSL_MAX_TIMEOUT_FRAMES 36000u
#ifndef DEBUG_DSL_MAX_EXECUTION_STEPS
#define DEBUG_DSL_MAX_EXECUTION_STEPS 1000000u
#endif
#define DEBUG_DSL_MAX_SOURCE_BYTES (1024u * 1024u)

typedef struct DebugDslVec3 {
  double x;
  double y;
  double z;
} DebugDslVec3;

typedef enum DebugDslValueType {
  DEBUG_DSL_VALUE_BOOL = 0,
  DEBUG_DSL_VALUE_NUMBER,
  DEBUG_DSL_VALUE_STRING,
  DEBUG_DSL_VALUE_VEC3
} DebugDslValueType;

typedef struct DebugDslValue {
  DebugDslValueType type;
  union {
    bool boolean;
    double number;
    const char *string;
    DebugDslVec3 vec3;
  } as;
} DebugDslValue;

typedef enum DebugDslErrorCode {
  DEBUG_DSL_ERROR_NONE = 0,
  DEBUG_DSL_ERROR_ARGUMENT,
  DEBUG_DSL_ERROR_ALLOCATION,
  DEBUG_DSL_ERROR_SYNTAX,
  DEBUG_DSL_ERROR_LIMIT,
  DEBUG_DSL_ERROR_UNDEFINED,
  DEBUG_DSL_ERROR_TYPE,
  DEBUG_DSL_ERROR_DIVIDE_BY_ZERO,
  DEBUG_DSL_ERROR_ASSERTION,
  DEBUG_DSL_ERROR_TIMEOUT,
  DEBUG_DSL_ERROR_CALLBACK
} DebugDslErrorCode;

typedef struct DebugDslError {
  DebugDslErrorCode code;
  size_t line;
  size_t column;
  char message[256];
} DebugDslError;

typedef struct DebugDslScript DebugDslScript;
typedef struct DebugDslExecutor DebugDslExecutor;
typedef struct DebugDslEnvironment DebugDslEnvironment;

typedef bool (*DebugDslResolveCallback)(void *userData, const char *name,
                                        DebugDslValue *outValue,
                                        DebugDslError *outError);

typedef enum DebugDslCommandResult {
  DEBUG_DSL_COMMAND_COMPLETE = 0,
  DEBUG_DSL_COMMAND_ERROR
} DebugDslCommandResult;

typedef DebugDslCommandResult (*DebugDslCommandCallback)(
    void *userData, const char *commandText, DebugDslError *outError);

typedef struct DebugDslCallbacks {
  void *userData;
  DebugDslResolveCallback resolve;
  DebugDslCommandCallback command;
} DebugDslCallbacks;

typedef enum DebugDslStepResult {
  DEBUG_DSL_STEP_RUNNING = 0,
  DEBUG_DSL_STEP_COMPLETE,
  DEBUG_DSL_STEP_EXIT,
  DEBUG_DSL_STEP_ERROR
} DebugDslStepResult;

void DebugDslErrorClear(DebugDslError *error);
const char *DebugDslErrorCodeName(DebugDslErrorCode code);

bool DebugDslParse(const char *source, DebugDslScript **outScript,
                   DebugDslError *outError);
void DebugDslScriptDestroy(DebugDslScript *script);
bool DebugDslScriptIsBatch(const DebugDslScript *script);

DebugDslExecutor *DebugDslExecutorCreate(const DebugDslScript *script,
                                         DebugDslCallbacks callbacks);
DebugDslExecutor *DebugDslExecutorCreateInEnvironment(
    const DebugDslScript *script, DebugDslCallbacks callbacks,
    DebugDslEnvironment *environment);
void DebugDslExecutorDestroy(DebugDslExecutor *executor);
DebugDslStepResult DebugDslExecutorStep(DebugDslExecutor *executor,
                                        DebugDslError *outError);
bool DebugDslExecutorFailed(const DebugDslExecutor *executor);
bool DebugDslExecutorFinished(const DebugDslExecutor *executor);
DebugDslStepResult DebugDslExecutorAbort(DebugDslExecutor *executor,
                                         DebugDslError *error);
int DebugDslExecutorExitCode(const DebugDslExecutor *executor);
DebugDslEnvironment *DebugDslEnvironmentCreate(void);
void DebugDslEnvironmentDestroy(DebugDslEnvironment *environment);

#endif
