#include "core/debug_dsl.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct DslHarness {
  bool gate;
  int commandCount;
  char commands[8][128];
  const char *failCommand;
} DslHarness;

static bool ResolveValue(void *userData, const char *name,
                         DebugDslValue *outValue, DebugDslError *outError) {
  DslHarness *harness = userData;
  (void)outError;
  if (strcmp(name, "gate") == 0) {
    *outValue = (DebugDslValue){.type = DEBUG_DSL_VALUE_BOOL,
                                .as.boolean = harness->gate};
    return true;
  }
  if (strcmp(name, "world.ready") == 0) {
    *outValue =
        (DebugDslValue){.type = DEBUG_DSL_VALUE_BOOL, .as.boolean = true};
    return true;
  }
  if (strcmp(name, "player.position") == 0) {
    *outValue = (DebugDslValue){.type = DEBUG_DSL_VALUE_VEC3,
                                .as.vec3 = {8.0, 12.0, -4.0}};
    return true;
  }
  return false;
}

static DebugDslCommandResult RunCommand(void *userData, const char *commandText,
                                        DebugDslError *outError) {
  DslHarness *harness = userData;
  if (harness->failCommand && strcmp(commandText, harness->failCommand) == 0) {
    outError->code = DEBUG_DSL_ERROR_CALLBACK;
    snprintf(outError->message, sizeof(outError->message), "rejected command");
    return DEBUG_DSL_COMMAND_ERROR;
  }
  assert(harness->commandCount < 8);
  snprintf(harness->commands[harness->commandCount],
           sizeof(harness->commands[harness->commandCount]), "%s", commandText);
  harness->commandCount++;
  return DEBUG_DSL_COMMAND_COMPLETE;
}

static DebugDslExecutor *ParseExecutor(const char *source, DslHarness *harness,
                                       DebugDslScript **outScript) {
  DebugDslError error;
  assert(DebugDslParse(source, outScript, &error));
  DebugDslExecutor *executor = DebugDslExecutorCreate(
      *outScript, (DebugDslCallbacks){.userData = harness,
                                      .resolve = ResolveValue,
                                      .command = RunCommand});
  assert(executor);
  return executor;
}

static DebugDslStepResult RunToEnd(DebugDslExecutor *executor,
                                   DebugDslError *error) {
  for (int step = 0; step < 1000; step++) {
    DebugDslStepResult result = DebugDslExecutorStep(executor, error);
    if (result != DEBUG_DSL_STEP_RUNNING)
      return result;
  }
  assert(!"DSL executor did not finish");
  return DEBUG_DSL_STEP_ERROR;
}

static void TestValuesExpressionsRepeatAndExit(void) {
  const char *source = "# values and precedence\r\n"
                       "let base = 2 + 3 * 4\r\n"
                       "let position = [1, 2 + 3, 6] * 2\r\n"
                       "let label = \"hello\\nworld\"\r\n"
                       "assert base == 14\r\n"
                       "assert position.x == 2 && position.y == 10\r\n"
                       "assert label == \"hello\\nworld\"\r\n"
                       "assert world.ready && player.position.z == -4\r\n"
                       "assert false && missing || true\r\n"
                       "repeat 2 {\r\n"
                       "  emit ${base} ${position.z}\r\n"
                       "}\r\n"
                       "exit";
  DslHarness harness = {0};
  DebugDslScript *script = NULL;
  DebugDslExecutor *executor = ParseExecutor(source, &harness, &script);
  assert(DebugDslScriptIsBatch(script));
  DebugDslError error;
  assert(RunToEnd(executor, &error) == DEBUG_DSL_STEP_EXIT);
  assert(error.code == DEBUG_DSL_ERROR_NONE);
  assert(harness.commandCount == 2);
  assert(strcmp(harness.commands[0], "emit 14 12") == 0);
  assert(strcmp(harness.commands[1], "emit 14 12") == 0);
  assert(DebugDslExecutorFinished(executor));
  assert(!DebugDslExecutorFailed(executor));
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);
}

static void TestTextualOperatorsVec3AndSemicolons(void) {
  const char *source = "let position = vec3(1, 2 + 3, 6) * 2;\n"
                       "assert true and not false;\n"
                       "assert false or position.x == 2 and "
                       "position.y == 10;\n"
                       "assert false and missing or true;\n"
                       "emit ${position.x} ${position.y} ${position.z};\n"
                       "repeat 1 {\n"
                       "  nested;\n"
                       "};\n";
  DslHarness harness = {0};
  DebugDslScript *script = NULL;
  DebugDslExecutor *executor = ParseExecutor(source, &harness, &script);
  DebugDslError error;
  assert(RunToEnd(executor, &error) == DEBUG_DSL_STEP_COMPLETE);
  assert(harness.commandCount == 2);
  assert(strcmp(harness.commands[0], "emit 2 10 12") == 0);
  assert(strcmp(harness.commands[1], "nested") == 0);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);
}

static void TestWaitCompletionAndTimeout(void) {
  DslHarness harness = {0};
  DebugDslScript *script = NULL;
  DebugDslExecutor *executor =
      ParseExecutor("wait until gate timeout 3\nready", &harness, &script);
  assert(!DebugDslScriptIsBatch(script));
  DebugDslError error;
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_RUNNING);
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_RUNNING);
  harness.gate = true;
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_RUNNING);
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_RUNNING);
  assert(RunToEnd(executor, &error) == DEBUG_DSL_STEP_COMPLETE);
  assert(harness.commandCount == 1);
  assert(strcmp(harness.commands[0], "ready") == 0);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);

  harness = (DslHarness){0};
  executor =
      ParseExecutor("wait until gate timeout 2\nnever", &harness, &script);
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_RUNNING);
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_RUNNING);
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_ERROR);
  assert(error.code == DEBUG_DSL_ERROR_TIMEOUT);
  assert(error.line == 1u);
  assert(harness.commandCount == 0);
  assert(DebugDslExecutorFailed(executor));
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);
}

static void TestExecutionErrors(void) {
  DslHarness harness = {0};
  DebugDslScript *script = NULL;
  DebugDslExecutor *executor =
      ParseExecutor("assert 1 == 2\nafter\nexit", &harness, &script);
  DebugDslError error;
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_ERROR);
  assert(error.code == DEBUG_DSL_ERROR_ASSERTION);
  assert(error.line == 1u);
  assert(harness.commandCount == 0);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);

  executor = ParseExecutor("assert 1 / 0 == 0", &harness, &script);
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_ERROR);
  assert(error.code == DEBUG_DSL_ERROR_DIVIDE_BY_ZERO);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);

  executor = ParseExecutor("assert unknown", &harness, &script);
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_ERROR);
  assert(error.code == DEBUG_DSL_ERROR_UNDEFINED);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);

  harness.failCommand = "reject";
  executor = ParseExecutor("reject\nafter", &harness, &script);
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_ERROR);
  assert(error.code == DEBUG_DSL_ERROR_CALLBACK);
  assert(error.line == 1u);
  assert(harness.commandCount == 0);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);

  executor = ParseExecutor(
      "assert vec3(1e308, 0, 0) * 2 == vec3(0, 0, 0)", &harness, &script);
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_ERROR);
  assert(error.code == DEBUG_DSL_ERROR_LIMIT);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);
}

static void AssertParseError(const char *source, DebugDslErrorCode code,
                             size_t line) {
  DebugDslScript *script = NULL;
  DebugDslError error;
  assert(!DebugDslParse(source, &script, &error));
  assert(!script);
  assert(error.code == code);
  assert(error.line == line);
}

static void TestParseErrorsAndFullParse(void) {
  AssertParseError("repeat 2 {\ncommand", DEBUG_DSL_ERROR_SYNTAX, 1u);
  AssertParseError("repeat 1 {\nexit\n}", DEBUG_DSL_ERROR_SYNTAX, 2u);
  AssertParseError("exit\nafter", DEBUG_DSL_ERROR_SYNTAX, 2u);
  AssertParseError("let bad-name = 1", DEBUG_DSL_ERROR_SYNTAX, 1u);
  AssertParseError("wait gate timeout 2", DEBUG_DSL_ERROR_SYNTAX, 1u);
  AssertParseError("assert \"unterminated", DEBUG_DSL_ERROR_SYNTAX, 1u);
  AssertParseError("let and = true", DEBUG_DSL_ERROR_SYNTAX, 1u);
  AssertParseError("let or = true", DEBUG_DSL_ERROR_SYNTAX, 1u);
  AssertParseError("let not = true", DEBUG_DSL_ERROR_SYNTAX, 1u);
  AssertParseError("let vec3 = true", DEBUG_DSL_ERROR_SYNTAX, 1u);

  char deepExpression[1024] = "assert true";
  for (unsigned index = 0u; index < DEBUG_DSL_MAX_EXPRESSION_DEPTH; index++) {
    strcat(deepExpression, " and true");
  }
  AssertParseError(deepExpression, DEBUG_DSL_ERROR_LIMIT, 1u);

  DslHarness harness = {0};
  DebugDslScript *script = NULL;
  DebugDslError error;
  assert(!DebugDslParse("first\nrepeat 1 {\n", &script, &error));
  assert(harness.commandCount == 0);
}

static void TestExecutionLimit(void) {
  DslHarness harness = {0};
  DebugDslScript *script = NULL;
  DebugDslExecutor *executor = ParseExecutor(
      "repeat 8 {\nrepeat 8 {\nassert true\n}\n}", &harness, &script);
  DebugDslError error;
  assert(RunToEnd(executor, &error) == DEBUG_DSL_STEP_ERROR);
  assert(error.code == DEBUG_DSL_ERROR_LIMIT);
  assert(strstr(error.message, "execution exceeds") != NULL);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);
}

static void TestRepeatLimitsAndZero(void) {
  DslHarness harness = {0};
  DebugDslScript *script = NULL;
  DebugDslExecutor *executor =
      ParseExecutor("repeat 0 {\nignored\n}\ndone", &harness, &script);
  DebugDslError error;
  assert(RunToEnd(executor, &error) == DEBUG_DSL_STEP_COMPLETE);
  assert(harness.commandCount == 1);
  assert(strcmp(harness.commands[0], "done") == 0);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);

  executor = ParseExecutor("repeat 3601 {\nignored\n}", &harness, &script);
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_ERROR);
  assert(error.code == DEBUG_DSL_ERROR_LIMIT);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);

  executor = ParseExecutor("repeat 1.5 {\nignored\n}", &harness, &script);
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_ERROR);
  assert(error.code == DEBUG_DSL_ERROR_LIMIT);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);
}

static void TestEmptyScriptAndErrorNames(void) {
  DslHarness harness = {0};
  DebugDslScript *script = NULL;
  DebugDslExecutor *executor = ParseExecutor("\n# empty\n", &harness, &script);
  DebugDslError error;
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_COMPLETE);
  assert(strcmp(DebugDslErrorCodeName(DEBUG_DSL_ERROR_TIMEOUT), "timeout") ==
         0);
  assert(strcmp(DebugDslErrorCodeName((DebugDslErrorCode)999), "unknown") == 0);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);
}

static void TestExitCode(void) {
  DslHarness harness = {0};
  DebugDslScript *script = NULL;
  DebugDslExecutor *executor = ParseExecutor("exit 7", &harness, &script);
  DebugDslError error;
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_EXIT);
  assert(DebugDslExecutorExitCode(executor) == 7);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);

  executor = ParseExecutor("exit 256", &harness, &script);
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_ERROR);
  assert(error.code == DEBUG_DSL_ERROR_LIMIT);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);
}

static void TestSharedEnvironment(void) {
  DslHarness harness = {0};
  DebugDslEnvironment *environment = DebugDslEnvironmentCreate();
  assert(environment);
  DebugDslError error;
  DebugDslScript *script = NULL;
  assert(DebugDslParse("let saved = 42\nlet saved_label = \"persisted\"",
                       &script, &error));
  DebugDslCallbacks callbacks = {.userData = &harness,
                                 .resolve = ResolveValue,
                                 .command = RunCommand};
  DebugDslExecutor *executor =
      DebugDslExecutorCreateInEnvironment(script, callbacks, environment);
  assert(executor);
  assert(RunToEnd(executor, &error) == DEBUG_DSL_STEP_COMPLETE);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);

  assert(DebugDslParse(
      "assert saved == 42\nassert saved_label == \"persisted\"", &script,
      &error));
  executor =
      DebugDslExecutorCreateInEnvironment(script, callbacks, environment);
  assert(executor);
  assert(RunToEnd(executor, &error) == DEBUG_DSL_STEP_COMPLETE);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);

  assert(DebugDslParse("let saved = 7", &script, &error));
  executor =
      DebugDslExecutorCreateInEnvironment(script, callbacks, environment);
  assert(executor);
  assert(DebugDslExecutorStep(executor, &error) == DEBUG_DSL_STEP_ERROR);
  assert(error.code == DEBUG_DSL_ERROR_UNDEFINED);
  assert(strstr(error.message, "already defined") != NULL);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);

  assert(DebugDslParse("assert saved == 42", &script, &error));
  executor =
      DebugDslExecutorCreateInEnvironment(script, callbacks, environment);
  assert(executor);
  assert(RunToEnd(executor, &error) == DEBUG_DSL_STEP_COMPLETE);
  DebugDslExecutorDestroy(executor);
  DebugDslScriptDestroy(script);
  DebugDslEnvironmentDestroy(environment);
}

int main(void) {
  TestValuesExpressionsRepeatAndExit();
  TestTextualOperatorsVec3AndSemicolons();
  TestWaitCompletionAndTimeout();
  TestExecutionErrors();
  TestParseErrorsAndFullParse();
  TestExecutionLimit();
  TestRepeatLimitsAndZero();
  TestEmptyScriptAndErrorNames();
  TestExitCode();
  TestSharedEnvironment();
  puts("debug DSL tests passed");
  return 0;
}
