typedef struct DebugDslOwnedValue {
  DebugDslValue value;
  char *ownedString;
} DebugDslOwnedValue;
typedef struct DebugDslVariable {
  char *name;
  DebugDslOwnedValue value;
} DebugDslVariable;
struct DebugDslEnvironment {
  DebugDslVariable *variables;
  size_t variableCount;
  size_t variableCapacity;
};
typedef struct DebugDslExecutionFrame {
  const DebugDslStatement *next;
  const DebugDslStatement *repeatBody;
  unsigned repeatRemaining;
} DebugDslExecutionFrame;
struct DebugDslExecutor {
  const DebugDslScript *script;
  DebugDslCallbacks callbacks;
  DebugDslEnvironment *environment;
  DebugDslExecutionFrame frames[DEBUG_DSL_MAX_REPEAT_DEPTH + 1u];
  unsigned depth;
  const DebugDslExpression *waitCondition;
  size_t waitLine;
  size_t waitColumn;
  unsigned waitTimeout;
  unsigned waitElapsed;
  unsigned executionSteps;
  DebugDslError terminalError;
  DebugDslStepResult finalResult;
  bool waiting;
  bool finished;
  bool failed;
  bool ownsEnvironment;
  int exitCode;
};
static void DebugDslOwnedValueRelease(DebugDslOwnedValue *value) {
  if (!value)
    return;
  free(value->ownedString);
  *value = (DebugDslOwnedValue){0};
}

static bool DebugDslOwnedValueAssign(DebugDslOwnedValue *target,
                                     DebugDslValue source, DebugDslError *error,
                                     size_t line, size_t column) {
  char *string = NULL;
  if (source.type == DEBUG_DSL_VALUE_STRING) {
    if (!source.as.string)
      source.as.string = "";
    string = DebugDslDuplicateRange(source.as.string, strlen(source.as.string));
    if (!string) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_ALLOCATION, line, column,
                       "out of memory while storing string");
      return false;
    }
    source.as.string = string;
  }
  DebugDslOwnedValueRelease(target);
  target->value = source;
  target->ownedString = string;
  return true;
}

static DebugDslVariable *DebugDslFindVariable(DebugDslExecutor *executor,
                                              const char *name) {
  DebugDslEnvironment *environment = executor->environment;
  for (size_t index = 0u; index < environment->variableCount; index++) {
    if (strcmp(environment->variables[index].name, name) == 0) {
      return &environment->variables[index];
    }
  }
  return NULL;
}

static bool DebugDslStoreVariable(DebugDslExecutor *executor, const char *name,
                                  DebugDslValue value, DebugDslError *error,
                                  size_t line, size_t column) {
  DebugDslVariable *variable = DebugDslFindVariable(executor, name);
  if (variable) {
    DebugDslSetError(error, DEBUG_DSL_ERROR_UNDEFINED, line, column,
                     "variable '%s' is already defined", name);
    return false;
  }
  DebugDslEnvironment *environment = executor->environment;
  if (environment->variableCount == environment->variableCapacity) {
    size_t capacity = environment->variableCapacity > 0u
                          ? environment->variableCapacity * 2u
                          : 16u;
    if (capacity < environment->variableCapacity ||
        capacity > SIZE_MAX / sizeof(*environment->variables)) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_LIMIT, line, column,
                       "too many variables");
      return false;
    }
    DebugDslVariable *variables =
        realloc(environment->variables, capacity * sizeof(*variables));
    if (!variables) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_ALLOCATION, line, column,
                       "out of memory while storing variable");
      return false;
    }
    environment->variables = variables;
    environment->variableCapacity = capacity;
  }
  variable = &environment->variables[environment->variableCount];
  *variable = (DebugDslVariable){0};
  variable->name = DebugDslDuplicateRange(name, strlen(name));
  if (!variable->name ||
      !DebugDslOwnedValueAssign(&variable->value, value, error, line, column)) {
    free(variable->name);
    variable->name = NULL;
    return false;
  }
  environment->variableCount++;
  return true;
}

static bool DebugDslResolveValue(DebugDslExecutor *executor, const char *name,
                                 DebugDslValue *outValue, DebugDslError *error,
                                 size_t line, size_t column) {
  DebugDslVariable *variable = DebugDslFindVariable(executor, name);
  if (variable) {
    *outValue = variable->value.value;
    return true;
  }
  if (executor->callbacks.resolve) {
    DebugDslError callbackError = {0};
    if (executor->callbacks.resolve(executor->callbacks.userData, name,
                                    outValue, &callbackError)) {
      if (outValue->type == DEBUG_DSL_VALUE_STRING && !outValue->as.string)
        outValue->as.string = "";
      return true;
    }
    if (callbackError.code != DEBUG_DSL_ERROR_NONE) {
      if (callbackError.line == 0u)
        callbackError.line = line;
      if (callbackError.column == 0u)
        callbackError.column = column;
      *error = callbackError;
      return false;
    }
  }
  DebugDslSetError(error, DEBUG_DSL_ERROR_UNDEFINED, line, column,
                   "undefined variable '%s'", name);
  return false;
}

static bool DebugDslValuesEqual(DebugDslValue left, DebugDslValue right,
                                bool *outEqual, DebugDslError *error,
                                size_t line, size_t column) {
  if (left.type != right.type) {
    DebugDslSetError(
        error, DEBUG_DSL_ERROR_TYPE, line, column, "cannot compare %s with %s",
        DebugDslValueTypeName(left.type), DebugDslValueTypeName(right.type));
    return false;
  }
  switch (left.type) {
  case DEBUG_DSL_VALUE_BOOL:
    *outEqual = left.as.boolean == right.as.boolean;
    return true;
  case DEBUG_DSL_VALUE_NUMBER:
    *outEqual = left.as.number == right.as.number;
    return true;
  case DEBUG_DSL_VALUE_STRING:
    *outEqual = strcmp(left.as.string ? left.as.string : "",
                       right.as.string ? right.as.string : "") == 0;
    return true;
  case DEBUG_DSL_VALUE_VEC3:
    *outEqual = left.as.vec3.x == right.as.vec3.x &&
                left.as.vec3.y == right.as.vec3.y &&
                left.as.vec3.z == right.as.vec3.z;
    return true;
  }
  return false;
}

static bool DebugDslEvaluate(DebugDslExecutor *executor,
                             const DebugDslExpression *expression,
                             DebugDslValue *outValue, DebugDslError *error);

static bool DebugDslEvaluateUnary(DebugDslExecutor *executor,
                                  const DebugDslExpression *expression,
                                  DebugDslValue *outValue,
                                  DebugDslError *error) {
  DebugDslValue operand = {0};
  if (!DebugDslEvaluate(executor, expression->as.unary.operand, &operand,
                        error))
    return false;
  switch (expression->as.unary.operation) {
  case DSL_TOKEN_BANG:
    if (operand.type != DEBUG_DSL_VALUE_BOOL)
      break;
    *outValue = (DebugDslValue){.type = DEBUG_DSL_VALUE_BOOL,
                                .as.boolean = !operand.as.boolean};
    return true;
  case DSL_TOKEN_PLUS:
    if (operand.type != DEBUG_DSL_VALUE_NUMBER)
      break;
    *outValue = operand;
    return true;
  case DSL_TOKEN_MINUS:
    if (operand.type == DEBUG_DSL_VALUE_NUMBER) {
      operand.as.number = -operand.as.number;
      *outValue = operand;
      return true;
    }
    if (operand.type == DEBUG_DSL_VALUE_VEC3) {
      operand.as.vec3.x = -operand.as.vec3.x;
      operand.as.vec3.y = -operand.as.vec3.y;
      operand.as.vec3.z = -operand.as.vec3.z;
      *outValue = operand;
      return true;
    }
    break;
  default:
    break;
  }
  DebugDslSetError(error, DEBUG_DSL_ERROR_TYPE, expression->line,
                   expression->column, "invalid unary operator for %s",
                   DebugDslValueTypeName(operand.type));
  return false;
}

static bool DebugDslEvaluateArithmetic(DebugDslTokenKind operation,
                                       DebugDslValue left, DebugDslValue right,
                                       DebugDslValue *outValue,
                                       DebugDslError *error, size_t line,
                                       size_t column) {
  if (left.type == DEBUG_DSL_VALUE_NUMBER &&
      right.type == DEBUG_DSL_VALUE_NUMBER) {
    double result = 0.0;
    switch (operation) {
    case DSL_TOKEN_PLUS:
      result = left.as.number + right.as.number;
      break;
    case DSL_TOKEN_MINUS:
      result = left.as.number - right.as.number;
      break;
    case DSL_TOKEN_STAR:
      result = left.as.number * right.as.number;
      break;
    case DSL_TOKEN_SLASH:
      if (right.as.number == 0.0) {
        DebugDslSetError(error, DEBUG_DSL_ERROR_DIVIDE_BY_ZERO, line, column,
                         "division by zero");
        return false;
      }
      result = left.as.number / right.as.number;
      break;
    case DSL_TOKEN_PERCENT:
      if (right.as.number == 0.0) {
        DebugDslSetError(error, DEBUG_DSL_ERROR_DIVIDE_BY_ZERO, line, column,
                         "remainder by zero");
        return false;
      }
      result = fmod(left.as.number, right.as.number);
      break;
    default:
      return false;
    }
    if (!isfinite(result)) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_LIMIT, line, column,
                       "numeric result is not finite");
      return false;
    }
    *outValue =
        (DebugDslValue){.type = DEBUG_DSL_VALUE_NUMBER, .as.number = result};
    return true;
  }
  DebugDslVec3 vec3 = {0};
  if ((operation == DSL_TOKEN_PLUS || operation == DSL_TOKEN_MINUS) &&
      left.type == DEBUG_DSL_VALUE_VEC3 && right.type == DEBUG_DSL_VALUE_VEC3) {
    double sign = operation == DSL_TOKEN_PLUS ? 1.0 : -1.0;
    vec3 = (DebugDslVec3){left.as.vec3.x + sign * right.as.vec3.x,
                          left.as.vec3.y + sign * right.as.vec3.y,
                          left.as.vec3.z + sign * right.as.vec3.z};
    goto finite_vec3;
  }
  if (operation == DSL_TOKEN_STAR && left.type == DEBUG_DSL_VALUE_NUMBER &&
      right.type == DEBUG_DSL_VALUE_VEC3) {
    DebugDslValue swap = left;
    left = right;
    right = swap;
  }
  if ((operation == DSL_TOKEN_STAR || operation == DSL_TOKEN_SLASH) &&
      left.type == DEBUG_DSL_VALUE_VEC3 &&
      right.type == DEBUG_DSL_VALUE_NUMBER) {
    if (operation == DSL_TOKEN_SLASH && right.as.number == 0.0) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_DIVIDE_BY_ZERO, line, column,
                       "vec3 division by zero");
      return false;
    }
    double scale =
        operation == DSL_TOKEN_STAR ? right.as.number : 1.0 / right.as.number;
    vec3 = (DebugDslVec3){left.as.vec3.x * scale, left.as.vec3.y * scale,
                          left.as.vec3.z * scale};
  finite_vec3:
    if (!isfinite(vec3.x) || !isfinite(vec3.y) || !isfinite(vec3.z)) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_LIMIT, line, column,
                       "vec3 result is not finite");
      return false;
    }
    *outValue = (DebugDslValue){.type = DEBUG_DSL_VALUE_VEC3, .as.vec3 = vec3};
    return true;
  }
  DebugDslSetError(error, DEBUG_DSL_ERROR_TYPE, line, column,
                   "invalid arithmetic between %s and %s",
                   DebugDslValueTypeName(left.type),
                   DebugDslValueTypeName(right.type));
  return false;
}

static bool DebugDslEvaluateBinary(DebugDslExecutor *executor,
                                   const DebugDslExpression *expression,
                                   DebugDslValue *outValue,
                                   DebugDslError *error) {
  DebugDslTokenKind operation = expression->as.binary.operation;
  DebugDslValue left = {0};
  if (!DebugDslEvaluate(executor, expression->as.binary.left, &left, error))
    return false;
  if (operation == DSL_TOKEN_AND_AND || operation == DSL_TOKEN_OR_OR) {
    if (left.type != DEBUG_DSL_VALUE_BOOL) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_TYPE, expression->line,
                       expression->column, "logical operand must be bool");
      return false;
    }
    if ((operation == DSL_TOKEN_AND_AND && !left.as.boolean) ||
        (operation == DSL_TOKEN_OR_OR && left.as.boolean)) {
      *outValue = left;
      return true;
    }
  }
  DebugDslValue right = {0};
  if (!DebugDslEvaluate(executor, expression->as.binary.right, &right, error))
    return false;
  if (operation == DSL_TOKEN_AND_AND || operation == DSL_TOKEN_OR_OR) {
    if (right.type != DEBUG_DSL_VALUE_BOOL) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_TYPE, expression->line,
                       expression->column, "logical operand must be bool");
      return false;
    }
    *outValue = right;
    return true;
  }
  if (operation == DSL_TOKEN_EQUAL_EQUAL || operation == DSL_TOKEN_BANG_EQUAL) {
    bool equal = false;
    if (!DebugDslValuesEqual(left, right, &equal, error, expression->line,
                             expression->column)) {
      return false;
    }
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_BOOL,
        .as.boolean = operation == DSL_TOKEN_EQUAL_EQUAL ? equal : !equal};
    return true;
  }
  if (operation == DSL_TOKEN_LESS || operation == DSL_TOKEN_LESS_EQUAL ||
      operation == DSL_TOKEN_GREATER || operation == DSL_TOKEN_GREATER_EQUAL) {
    if (left.type != DEBUG_DSL_VALUE_NUMBER ||
        right.type != DEBUG_DSL_VALUE_NUMBER) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_TYPE, expression->line,
                       expression->column,
                       "ordered comparison requires numbers");
      return false;
    }
    bool result =
        operation == DSL_TOKEN_LESS         ? left.as.number < right.as.number
        : operation == DSL_TOKEN_LESS_EQUAL ? left.as.number <= right.as.number
        : operation == DSL_TOKEN_GREATER    ? left.as.number > right.as.number
                                            : left.as.number >= right.as.number;
    *outValue =
        (DebugDslValue){.type = DEBUG_DSL_VALUE_BOOL, .as.boolean = result};
    return true;
  }
  return DebugDslEvaluateArithmetic(operation, left, right, outValue, error,
                                    expression->line, expression->column);
}

static bool DebugDslEvaluate(DebugDslExecutor *executor,
                             const DebugDslExpression *expression,
                             DebugDslValue *outValue, DebugDslError *error) {
  switch (expression->kind) {
  case DSL_EXPR_LITERAL:
    *outValue = expression->as.literal;
    return true;
  case DSL_EXPR_VARIABLE:
    return DebugDslResolveValue(executor, expression->as.variable, outValue,
                                error, expression->line, expression->column);
  case DSL_EXPR_FIELD: {
    DebugDslValue base = {0};
    if (!DebugDslEvaluate(executor, expression->as.field.base, &base, error))
      return false;
    if (base.type != DEBUG_DSL_VALUE_VEC3) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_TYPE, expression->line,
                       expression->column, "field access requires vec3");
      return false;
    }
    double component = expression->as.field.component == 'x'   ? base.as.vec3.x
                       : expression->as.field.component == 'y' ? base.as.vec3.y
                                                               : base.as.vec3.z;
    *outValue =
        (DebugDslValue){.type = DEBUG_DSL_VALUE_NUMBER, .as.number = component};
    return true;
  }
  case DSL_EXPR_UNARY:
    return DebugDslEvaluateUnary(executor, expression, outValue, error);
  case DSL_EXPR_BINARY:
    return DebugDslEvaluateBinary(executor, expression, outValue, error);
  case DSL_EXPR_VEC3: {
    double components[3] = {0};
    for (int index = 0; index < 3; index++) {
      DebugDslValue component = {0};
      if (!DebugDslEvaluate(executor, expression->as.components[index],
                            &component, error))
        return false;
      if (component.type != DEBUG_DSL_VALUE_NUMBER) {
        DebugDslSetError(error, DEBUG_DSL_ERROR_TYPE, expression->line,
                         expression->column, "vec3 components must be numbers");
        return false;
      }
      components[index] = component.as.number;
    }
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_VEC3,
        .as.vec3 = {components[0], components[1], components[2]}};
    return true;
  }
  }
  return false;
}

static bool DebugDslNumberToUnsigned(DebugDslValue value, unsigned maximum,
                                     const char *description,
                                     unsigned *outValue, DebugDslError *error,
                                     size_t line, size_t column) {
  if (value.type != DEBUG_DSL_VALUE_NUMBER || value.as.number < 0.0 ||
      value.as.number > (double)maximum ||
      floor(value.as.number) != value.as.number) {
    DebugDslSetError(error, DEBUG_DSL_ERROR_LIMIT, line, column,
                     "%s must be an integer from 0 to %u", description,
                     maximum);
    return false;
  }
  *outValue = (unsigned)value.as.number;
  return true;
}

static bool DebugDslAppendText(char *output, size_t outputSize, size_t *length,
                               const char *text, size_t textLength) {
  if (textLength > outputSize - 1u - *length)
    return false;
  memcpy(output + *length, text, textLength);
  *length += textLength;
  output[*length] = '\0';
  return true;
}

static bool DebugDslFormatValue(DebugDslValue value, char *output,
                                size_t outputSize) {
  if (!output || outputSize == 0u)
    return false;
  switch (value.type) {
  case DEBUG_DSL_VALUE_BOOL:
    snprintf(output, outputSize, "%s", value.as.boolean ? "true" : "false");
    return strlen(output) < outputSize;
  case DEBUG_DSL_VALUE_NUMBER: {
    int written = snprintf(output, outputSize, "%.17g", value.as.number);
    return written >= 0 && (size_t)written < outputSize;
  }
  case DEBUG_DSL_VALUE_STRING: {
    const char *string = value.as.string ? value.as.string : "";
    size_t length = strlen(string);
    if (length >= outputSize)
      return false;
    memcpy(output, string, length + 1u);
    return true;
  }
  case DEBUG_DSL_VALUE_VEC3: {
    int written = snprintf(output, outputSize, "[%.17g,%.17g,%.17g]",
                           value.as.vec3.x, value.as.vec3.y, value.as.vec3.z);
    return written >= 0 && (size_t)written < outputSize;
  }
  }
  return false;
}

static bool DebugDslInterpolateCommand(DebugDslExecutor *executor,
                                       const DebugDslStatement *statement,
                                       char output[DEBUG_DSL_MAX_COMMAND_TEXT],
                                       DebugDslError *error) {
  const char *input = statement->as.command;
  size_t outputLength = 0u;
  output[0] = '\0';
  for (size_t index = 0u; input[index] != '\0';) {
    if (input[index] != '$' || input[index + 1u] != '{') {
      if (!DebugDslAppendText(output, DEBUG_DSL_MAX_COMMAND_TEXT, &outputLength,
                              input + index, 1u)) {
        DebugDslSetError(error, DEBUG_DSL_ERROR_LIMIT, statement->line,
                         statement->column + index,
                         "expanded command exceeds %u bytes",
                         DEBUG_DSL_MAX_COMMAND_TEXT - 1u);
        return false;
      }
      index++;
      continue;
    }
    size_t expressionStart = index + 2u;
    size_t end = expressionStart;
    while (input[end] != '\0' && input[end] != '}')
      end++;
    if (input[end] != '}') {
      DebugDslSetError(error, DEBUG_DSL_ERROR_SYNTAX, statement->line,
                       statement->column + index,
                       "unterminated command interpolation");
      return false;
    }
    char *expressionText =
        DebugDslDuplicateRange(input + expressionStart, end - expressionStart);
    if (!expressionText) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_ALLOCATION, statement->line,
                       statement->column + index,
                       "out of memory while expanding command");
      return false;
    }
    DebugDslExpression *expression =
        DebugDslParseExpression(expressionText, statement->line,
                                statement->column + expressionStart, error);
    free(expressionText);
    if (!expression)
      return false;
    DebugDslValue value = {0};
    bool evaluated = DebugDslEvaluate(executor, expression, &value, error);
    DebugDslExpressionDestroy(expression);
    if (!evaluated)
      return false;
    char formatted[256];
    if (!DebugDslFormatValue(value, formatted, sizeof(formatted)) ||
        !DebugDslAppendText(output, DEBUG_DSL_MAX_COMMAND_TEXT, &outputLength,
                            formatted, strlen(formatted))) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_LIMIT, statement->line,
                       statement->column + index,
                       "expanded command value is too long");
      return false;
    }
    index = end + 1u;
  }
  return true;
}
DebugDslEnvironment *DebugDslEnvironmentCreate(void) {
  return calloc(1u, sizeof(DebugDslEnvironment));
}
void DebugDslEnvironmentDestroy(DebugDslEnvironment *environment) {
  if (!environment)
    return;
  for (size_t index = 0u; index < environment->variableCount; index++) {
    free(environment->variables[index].name);
    DebugDslOwnedValueRelease(&environment->variables[index].value);
  }
  free(environment->variables);
  free(environment);
}
DebugDslExecutor *DebugDslExecutorCreateInEnvironment(
    const DebugDslScript *script, DebugDslCallbacks callbacks,
    DebugDslEnvironment *environment) {
  if (!script)
    return NULL;
  DebugDslExecutor *executor = calloc(1u, sizeof(*executor));
  if (!executor)
    return NULL;
  executor->environment = environment ? environment : DebugDslEnvironmentCreate();
  if (!executor->environment) {
    free(executor);
    return NULL;
  }
  executor->ownsEnvironment = environment == NULL;
  executor->script = script;
  executor->callbacks = callbacks;
  executor->frames[0].next = script->statements;
  executor->depth = 1u;
  executor->finalResult = DEBUG_DSL_STEP_RUNNING;
  return executor;
}
DebugDslExecutor *DebugDslExecutorCreate(const DebugDslScript *script,
                                         DebugDslCallbacks callbacks) {
  return DebugDslExecutorCreateInEnvironment(script, callbacks, NULL);
}
void DebugDslExecutorDestroy(DebugDslExecutor *executor) {
  if (!executor)
    return;
  if (executor->ownsEnvironment)
    DebugDslEnvironmentDestroy(executor->environment);
  free(executor);
}
bool DebugDslExecutorFailed(const DebugDslExecutor *executor) {
  return executor && executor->failed;
}
bool DebugDslExecutorFinished(const DebugDslExecutor *executor) {
  return executor && executor->finished;
}
int DebugDslExecutorExitCode(const DebugDslExecutor *executor) {
  return executor ? executor->exitCode : 0;
}
static DebugDslStepResult DebugDslExecutorFail(DebugDslExecutor *executor,
                                               DebugDslError *error) {
  executor->failed = true;
  executor->finished = true;
  executor->finalResult = DEBUG_DSL_STEP_ERROR;
  if (error->code == DEBUG_DSL_ERROR_NONE) {
    DebugDslSetError(error, DEBUG_DSL_ERROR_CALLBACK, 0u, 0u,
                     "debug DSL execution failed");
  }
  return DEBUG_DSL_STEP_ERROR;
}
DebugDslStepResult DebugDslExecutorAbort(DebugDslExecutor *executor,
                                         DebugDslError *error) {
  if (!executor || !error) return DEBUG_DSL_STEP_ERROR;
  return DebugDslExecutorFail(executor, error);
}
static bool DebugDslNormalizeFrames(DebugDslExecutor *executor) {
  while (executor->depth > 0u) {
    DebugDslExecutionFrame *frame = &executor->frames[executor->depth - 1u];
    if (frame->next)
      return true;
    if (frame->repeatBody && frame->repeatRemaining > 1u) {
      frame->repeatRemaining--;
      frame->next = frame->repeatBody;
      return true;
    }
    executor->depth--;
  }
  return false;
}

static DebugDslStepResult DebugDslExecutorWaitStep(DebugDslExecutor *executor,
                                                   DebugDslError *error) {
  DebugDslValue condition = {0};
  if (!DebugDslEvaluate(executor, executor->waitCondition, &condition, error)) {
    return DebugDslExecutorFail(executor, error);
  }
  if (condition.type != DEBUG_DSL_VALUE_BOOL) {
    DebugDslSetError(error, DEBUG_DSL_ERROR_TYPE, executor->waitLine,
                     executor->waitColumn, "wait condition must be bool");
    return DebugDslExecutorFail(executor, error);
  }
  if (condition.as.boolean) {
    executor->waiting = false;
    executor->waitCondition = NULL;
    return DEBUG_DSL_STEP_RUNNING;
  }
  executor->waitElapsed++;
  if (executor->waitElapsed >= executor->waitTimeout) {
    DebugDslSetError(error, DEBUG_DSL_ERROR_TIMEOUT, executor->waitLine,
                     executor->waitColumn,
                     "wait condition timed out after %u frames",
                     executor->waitTimeout);
    return DebugDslExecutorFail(executor, error);
  }
  return DEBUG_DSL_STEP_RUNNING;
}

DebugDslStepResult DebugDslExecutorStep(DebugDslExecutor *executor,
                                        DebugDslError *outError) {
  DebugDslError localError = {0};
  DebugDslError *error = outError ? outError : &localError;
  DebugDslErrorClear(error);
  if (!executor) {
    DebugDslSetError(error, DEBUG_DSL_ERROR_ARGUMENT, 0u, 0u,
                     "executor is required");
    return DEBUG_DSL_STEP_ERROR;
  }
  if (executor->finished)
    return executor->finalResult;
  if (executor->waiting)
    return DebugDslExecutorWaitStep(executor, error);
  if (!DebugDslNormalizeFrames(executor)) {
    executor->finished = true;
    executor->finalResult = DEBUG_DSL_STEP_COMPLETE;
    return DEBUG_DSL_STEP_COMPLETE;
  }

  DebugDslExecutionFrame *frame = &executor->frames[executor->depth - 1u];
  const DebugDslStatement *statement = frame->next;
  frame->next = statement->next;
  if (executor->executionSteps >= DEBUG_DSL_MAX_EXECUTION_STEPS) {
    DebugDslSetError(error, DEBUG_DSL_ERROR_LIMIT, statement->line,
                     statement->column, "execution exceeds %u statements",
                     DEBUG_DSL_MAX_EXECUTION_STEPS);
    return DebugDslExecutorFail(executor, error);
  }
  executor->executionSteps++;
  switch (statement->kind) {
  case DSL_STATEMENT_LET: {
    DebugDslValue value = {0};
    if (!DebugDslEvaluate(executor, statement->as.let.value, &value, error) ||
        !DebugDslStoreVariable(executor, statement->as.let.name, value, error,
                               statement->line, statement->column)) {
      return DebugDslExecutorFail(executor, error);
    }
    return DEBUG_DSL_STEP_RUNNING;
  }
  case DSL_STATEMENT_COMMAND: {
    if (!executor->callbacks.command) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_CALLBACK, statement->line,
                       statement->column, "no command callback is installed");
      return DebugDslExecutorFail(executor, error);
    }
    char command[DEBUG_DSL_MAX_COMMAND_TEXT];
    if (!DebugDslInterpolateCommand(executor, statement, command, error)) {
      return DebugDslExecutorFail(executor, error);
    }
    DebugDslError callbackError = {0};
    DebugDslCommandResult result = executor->callbacks.command(
        executor->callbacks.userData, command, &callbackError);
    if (result == DEBUG_DSL_COMMAND_ERROR) {
      if (callbackError.code == DEBUG_DSL_ERROR_NONE) {
        DebugDslSetError(error, DEBUG_DSL_ERROR_CALLBACK, statement->line,
                         statement->column, "command failed: %s", command);
      } else {
        if (callbackError.line == 0u)
          callbackError.line = statement->line;
        if (callbackError.column == 0u) {
          callbackError.column = statement->column;
        }
        *error = callbackError;
      }
      return DebugDslExecutorFail(executor, error);
    }
    return DEBUG_DSL_STEP_RUNNING;
  }
  case DSL_STATEMENT_ASSERT: {
    DebugDslValue value = {0};
    if (!DebugDslEvaluate(executor, statement->as.assertion, &value, error)) {
      return DebugDslExecutorFail(executor, error);
    }
    if (value.type != DEBUG_DSL_VALUE_BOOL) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_TYPE, statement->line,
                       statement->column, "assert expression must be bool");
      return DebugDslExecutorFail(executor, error);
    }
    if (!value.as.boolean) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_ASSERTION, statement->line,
                       statement->column, "assertion failed");
      return DebugDslExecutorFail(executor, error);
    }
    return DEBUG_DSL_STEP_RUNNING;
  }
  case DSL_STATEMENT_WAIT: {
    DebugDslValue timeout = {0};
    unsigned timeoutFrames = 0u;
    if (!DebugDslEvaluate(executor, statement->as.wait.timeout, &timeout,
                          error) ||
        !DebugDslNumberToUnsigned(timeout, DEBUG_DSL_MAX_TIMEOUT_FRAMES,
                                  "wait timeout", &timeoutFrames, error,
                                  statement->line, statement->column)) {
      return DebugDslExecutorFail(executor, error);
    }
    if (timeoutFrames == 0u) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_LIMIT, statement->line,
                       statement->column,
                       "wait timeout must be at least one frame");
      return DebugDslExecutorFail(executor, error);
    }
    DebugDslValue condition = {0};
    if (!DebugDslEvaluate(executor, statement->as.wait.condition, &condition,
                          error)) {
      return DebugDslExecutorFail(executor, error);
    }
    if (condition.type != DEBUG_DSL_VALUE_BOOL) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_TYPE, statement->line,
                       statement->column, "wait condition must be bool");
      return DebugDslExecutorFail(executor, error);
    }
    if (!condition.as.boolean) {
      executor->waiting = true;
      executor->waitCondition = statement->as.wait.condition;
      executor->waitLine = statement->line;
      executor->waitColumn = statement->column;
      executor->waitTimeout = timeoutFrames;
      executor->waitElapsed = 0u;
    }
    return DEBUG_DSL_STEP_RUNNING;
  }
  case DSL_STATEMENT_REPEAT: {
    DebugDslValue count = {0};
    unsigned repeatCount = 0u;
    if (!DebugDslEvaluate(executor, statement->as.repeat.count, &count,
                          error) ||
        !DebugDslNumberToUnsigned(count, DEBUG_DSL_MAX_REPEAT_COUNT,
                                  "repeat count", &repeatCount, error,
                                  statement->line, statement->column)) {
      return DebugDslExecutorFail(executor, error);
    }
    if (repeatCount == 0u || !statement->as.repeat.body) {
      return DEBUG_DSL_STEP_RUNNING;
    }
    if (executor->depth >= DEBUG_DSL_MAX_REPEAT_DEPTH + 1u) {
      DebugDslSetError(error, DEBUG_DSL_ERROR_LIMIT, statement->line,
                       statement->column, "repeat execution nesting exceeds %u",
                       DEBUG_DSL_MAX_REPEAT_DEPTH);
      return DebugDslExecutorFail(executor, error);
    }
    executor->frames[executor->depth++] =
        (DebugDslExecutionFrame){.next = statement->as.repeat.body,
                                 .repeatBody = statement->as.repeat.body,
                                 .repeatRemaining = repeatCount};
    return DEBUG_DSL_STEP_RUNNING;
  }
  case DSL_STATEMENT_EXIT: {
    DebugDslValue code = {0};
    unsigned exitCode = 0u;
    if (!DebugDslEvaluate(executor, statement->as.exitCode, &code, error) ||
        !DebugDslNumberToUnsigned(code, 255u, "exit code", &exitCode, error,
                                  statement->line, statement->column)) {
      return DebugDslExecutorFail(executor, error);
    }
    executor->exitCode = (int)exitCode;
    executor->finished = true;
    executor->finalResult = DEBUG_DSL_STEP_EXIT;
    return DEBUG_DSL_STEP_EXIT;
  }
  }
  return DEBUG_DSL_STEP_RUNNING;
}
