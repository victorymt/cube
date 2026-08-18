#ifndef VOXELCRAFT_DEBUG_DSL_INTERNAL_H
#define VOXELCRAFT_DEBUG_DSL_INTERNAL_H

#include "core/debug_dsl.h"

void DebugDslSetError(DebugDslError *error, DebugDslErrorCode code,
                      size_t line, size_t column, const char *format, ...);
const char *DebugDslValueTypeName(DebugDslValueType type);

#endif
