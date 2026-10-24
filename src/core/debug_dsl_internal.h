#ifndef VOXELCRAFT_DEBUG_DSL_INTERNAL_H
#define VOXELCRAFT_DEBUG_DSL_INTERNAL_H

#include "core/debug_dsl.h"

#include <stdbool.h>
#include <stddef.h>


const char *DebugDslValueTypeName(DebugDslValueType type);

/* The parsed representation and the lexer state, shared by the parser
 * (debug_dsl.c) and the statement executor (debug_dsl_execute.c). */
typedef enum DebugDslExpressionKind {
  DSL_EXPR_LITERAL = 0,
  DSL_EXPR_VARIABLE,
  DSL_EXPR_FIELD,
  DSL_EXPR_UNARY,
  DSL_EXPR_BINARY,
  DSL_EXPR_VEC3
} DebugDslExpressionKind;
typedef enum DebugDslTokenKind {
  DSL_TOKEN_END = 0,
  DSL_TOKEN_NUMBER,
  DSL_TOKEN_STRING,
  DSL_TOKEN_IDENTIFIER,
  DSL_TOKEN_TRUE,
  DSL_TOKEN_FALSE,
  DSL_TOKEN_LEFT_PAREN,
  DSL_TOKEN_RIGHT_PAREN,
  DSL_TOKEN_LEFT_BRACKET,
  DSL_TOKEN_RIGHT_BRACKET,
  DSL_TOKEN_COMMA,
  DSL_TOKEN_DOT,
  DSL_TOKEN_PLUS,
  DSL_TOKEN_MINUS,
  DSL_TOKEN_STAR,
  DSL_TOKEN_SLASH,
  DSL_TOKEN_PERCENT,
  DSL_TOKEN_BANG,
  DSL_TOKEN_EQUAL_EQUAL,
  DSL_TOKEN_BANG_EQUAL,
  DSL_TOKEN_LESS,
  DSL_TOKEN_LESS_EQUAL,
  DSL_TOKEN_GREATER,
  DSL_TOKEN_GREATER_EQUAL,
  DSL_TOKEN_AND_AND,
  DSL_TOKEN_OR_OR,
  DSL_TOKEN_INVALID
} DebugDslTokenKind;
typedef struct DebugDslExpression DebugDslExpression;
struct DebugDslExpression {
  DebugDslExpressionKind kind;
  size_t line;
  size_t column;
  unsigned depth;
  union {
    DebugDslValue literal;
    char *variable;
    struct {
      DebugDslExpression *base;
      char component;
    } field;
    struct {
      DebugDslTokenKind operation;
      DebugDslExpression *operand;
    } unary;
    struct {
      DebugDslTokenKind operation;
      DebugDslExpression *left;
      DebugDslExpression *right;
    } binary;
    DebugDslExpression *components[3];
  } as;
};
typedef enum DebugDslStatementKind {
  DSL_STATEMENT_LET = 0,
  DSL_STATEMENT_COMMAND,
  DSL_STATEMENT_ASSERT,
  DSL_STATEMENT_WAIT,
  DSL_STATEMENT_REPEAT,
  DSL_STATEMENT_EXIT
} DebugDslStatementKind;
typedef struct DebugDslStatement DebugDslStatement;
struct DebugDslStatement {
  DebugDslStatementKind kind;
  size_t line;
  size_t column;
  DebugDslStatement *next;
  union {
    struct {
      char *name;
      DebugDslExpression *value;
    } let;
    char *command;
    DebugDslExpression *assertion;
    struct {
      DebugDslExpression *condition;
      DebugDslExpression *timeout;
    } wait;
    struct {
      DebugDslExpression *count;
      DebugDslStatement *body;
    } repeat;
    DebugDslExpression *exitCode;
  } as;
};
struct DebugDslScript {
  DebugDslStatement *statements;
  bool batch;
};
typedef struct DebugDslToken {
  DebugDslTokenKind kind;
  const char *start;
  size_t length;
  size_t column;
  double number;
  char *string;
} DebugDslToken;
typedef struct DebugDslLexer {
  const char *source;
  size_t offset;
  size_t line;
  size_t baseColumn;
  DebugDslToken token;
  DebugDslError *error;
  bool failed;
} DebugDslLexer;

/* Shared between the parser and the executor. */
char *DebugDslDuplicateRange(const char *text, size_t length);
DebugDslExpression *DebugDslParseExpression(const char *text, size_t line,
                                            size_t column,
                                            DebugDslError *error);
void DebugDslExpressionDestroy(DebugDslExpression *expression);
void DebugDslSetError(DebugDslError *error, DebugDslErrorCode code,
                      size_t line, size_t column, const char *format, ...);

#endif
