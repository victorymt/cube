#include "core/debug_dsl_internal.h"

#include <stdarg.h>
#include <stdio.h>

void DebugDslSetError(DebugDslError *error, DebugDslErrorCode code,
                      size_t line, size_t column, const char *format, ...) {
  if (!error || error->code != DEBUG_DSL_ERROR_NONE)
    return;
  error->code = code;
  error->line = line;
  error->column = column;
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(error->message, sizeof(error->message), format, arguments);
  va_end(arguments);
}

void DebugDslErrorClear(DebugDslError *error) {
  if (error)
    *error = (DebugDslError){0};
}

const char *DebugDslErrorCodeName(DebugDslErrorCode code) {
  switch (code) {
  case DEBUG_DSL_ERROR_NONE:
    return "none";
  case DEBUG_DSL_ERROR_ARGUMENT:
    return "argument";
  case DEBUG_DSL_ERROR_ALLOCATION:
    return "allocation";
  case DEBUG_DSL_ERROR_SYNTAX:
    return "syntax";
  case DEBUG_DSL_ERROR_LIMIT:
    return "limit";
  case DEBUG_DSL_ERROR_UNDEFINED:
    return "undefined";
  case DEBUG_DSL_ERROR_TYPE:
    return "type";
  case DEBUG_DSL_ERROR_DIVIDE_BY_ZERO:
    return "divide_by_zero";
  case DEBUG_DSL_ERROR_ASSERTION:
    return "assertion";
  case DEBUG_DSL_ERROR_TIMEOUT:
    return "timeout";
  case DEBUG_DSL_ERROR_CALLBACK:
    return "callback";
  }
  return "unknown";
}

const char *DebugDslValueTypeName(DebugDslValueType type) {
  switch (type) {
  case DEBUG_DSL_VALUE_BOOL:
    return "bool";
  case DEBUG_DSL_VALUE_NUMBER:
    return "number";
  case DEBUG_DSL_VALUE_STRING:
    return "string";
  case DEBUG_DSL_VALUE_VEC3:
    return "vec3";
  }
  return "unknown";
}
