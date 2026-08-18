#include "core/debug_dsl_internal.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
typedef struct DebugDslExpressionParser {
  DebugDslLexer lexer;
  unsigned nesting;
} DebugDslExpressionParser;
typedef struct DebugDslSourceLine {
  const char *text;
  size_t length;
  size_t line;
} DebugDslSourceLine;
typedef struct DebugDslParser {
  DebugDslSourceLine *lines;
  size_t lineCount;
  size_t index;
  DebugDslError *error;
} DebugDslParser;
static char *DebugDslDuplicateRange(const char *text, size_t length) {
  if (!text || length == (size_t)-1)
    return NULL;
  char *copy = malloc(length + 1u);
  if (!copy)
    return NULL;
  memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}
static DebugDslExpression *DebugDslExpressionCreate(DebugDslExpressionKind kind,
                                                    size_t line, size_t column,
                                                    DebugDslError *error) {
  DebugDslExpression *expression = calloc(1u, sizeof(*expression));
  if (!expression) {
    DebugDslSetError(error, DEBUG_DSL_ERROR_ALLOCATION, line, column,
                     "out of memory while parsing expression");
    return NULL;
  }
  expression->kind = kind;
  expression->line = line;
  expression->column = column;
  expression->depth = 1u;
  return expression;
}
static void DebugDslExpressionDestroy(DebugDslExpression *expression) {
  if (!expression)
    return;
  switch (expression->kind) {
  case DSL_EXPR_LITERAL:
    if (expression->as.literal.type == DEBUG_DSL_VALUE_STRING) {
      free((char *)expression->as.literal.as.string);
    }
    break;
  case DSL_EXPR_VARIABLE:
    free(expression->as.variable);
    break;
  case DSL_EXPR_FIELD:
    DebugDslExpressionDestroy(expression->as.field.base);
    break;
  case DSL_EXPR_UNARY:
    DebugDslExpressionDestroy(expression->as.unary.operand);
    break;
  case DSL_EXPR_BINARY:
    DebugDslExpressionDestroy(expression->as.binary.left);
    DebugDslExpressionDestroy(expression->as.binary.right);
    break;
  case DSL_EXPR_VEC3:
    for (int index = 0; index < 3; index++) {
      DebugDslExpressionDestroy(expression->as.components[index]);
    }
    break;
  }
  free(expression);
}
static void DebugDslTokenRelease(DebugDslToken *token) {
  if (!token)
    return;
  free(token->string);
  token->string = NULL;
}

static bool DebugDslLexerString(DebugDslLexer *lexer, size_t startOffset,
                                size_t column) {
  size_t capacity = strlen(lexer->source + lexer->offset) + 1u;
  char *value = malloc(capacity);
  if (!value) {
    DebugDslSetError(lexer->error, DEBUG_DSL_ERROR_ALLOCATION, lexer->line,
                     column, "out of memory while parsing string");
    lexer->failed = true;
    return false;
  }
  size_t length = 0u;
  while (lexer->source[lexer->offset] != '\0' &&
         lexer->source[lexer->offset] != '"') {
    unsigned char current = (unsigned char)lexer->source[lexer->offset++];
    if (current == '\\') {
      char escaped = lexer->source[lexer->offset++];
      if (escaped == '\0')
        break;
      switch (escaped) {
      case 'n':
        current = '\n';
        break;
      case 'r':
        current = '\r';
        break;
      case 't':
        current = '\t';
        break;
      case '"':
        current = '"';
        break;
      case '\\':
        current = '\\';
        break;
      default:
        free(value);
        DebugDslSetError(lexer->error, DEBUG_DSL_ERROR_SYNTAX, lexer->line,
                         lexer->baseColumn + lexer->offset - 1u,
                         "unsupported string escape \\%c", escaped);
        lexer->failed = true;
        return false;
      }
    }
    value[length++] = (char)current;
  }
  if (lexer->source[lexer->offset] != '"') {
    free(value);
    DebugDslSetError(lexer->error, DEBUG_DSL_ERROR_SYNTAX, lexer->line, column,
                     "unterminated string literal");
    lexer->failed = true;
    return false;
  }
  lexer->offset++;
  value[length] = '\0';
  lexer->token = (DebugDslToken){.kind = DSL_TOKEN_STRING,
                                 .start = lexer->source + startOffset,
                                 .length = lexer->offset - startOffset,
                                 .column = column,
                                 .string = value};
  return true;
}
static void DebugDslLexerNext(DebugDslLexer *lexer) {
  DebugDslTokenRelease(&lexer->token);
  if (lexer->failed) {
    lexer->token = (DebugDslToken){.kind = DSL_TOKEN_INVALID};
    return;
  }
  while (isspace((unsigned char)lexer->source[lexer->offset])) {
    lexer->offset++;
  }
  size_t start = lexer->offset;
  size_t column = lexer->baseColumn + start;
  char current = lexer->source[lexer->offset++];
  lexer->token = (DebugDslToken){.kind = DSL_TOKEN_INVALID,
                                 .start = lexer->source + start,
                                 .length = 1u,
                                 .column = column};
  if (current == '\0') {
    lexer->offset--;
    lexer->token.kind = DSL_TOKEN_END;
    lexer->token.length = 0u;
    return;
  }
  if (isdigit((unsigned char)current) ||
      (current == '.' &&
       isdigit((unsigned char)lexer->source[lexer->offset]))) {
    char *end = NULL;
    errno = 0;
    double number = strtod(lexer->source + start, &end);
    if (end == lexer->source + start || errno == ERANGE || !isfinite(number)) {
      DebugDslSetError(lexer->error, DEBUG_DSL_ERROR_SYNTAX, lexer->line,
                       column, "number literal must be finite");
      lexer->failed = true;
      return;
    }
    lexer->offset = (size_t)(end - lexer->source);
    lexer->token.kind = DSL_TOKEN_NUMBER;
    lexer->token.length = lexer->offset - start;
    lexer->token.number = number;
    return;
  }
  if (isalpha((unsigned char)current) || current == '_') {
    while (isalnum((unsigned char)lexer->source[lexer->offset]) ||
           lexer->source[lexer->offset] == '_') {
      lexer->offset++;
    }
    lexer->token.kind = DSL_TOKEN_IDENTIFIER;
    lexer->token.length = lexer->offset - start;
    if (lexer->token.length == 4u &&
        strncmp(lexer->token.start, "true", 4u) == 0) {
      lexer->token.kind = DSL_TOKEN_TRUE;
    } else if (lexer->token.length == 5u &&
               strncmp(lexer->token.start, "false", 5u) == 0) {
      lexer->token.kind = DSL_TOKEN_FALSE;
    } else if (lexer->token.length == 3u &&
               strncmp(lexer->token.start, "and", 3u) == 0) {
      lexer->token.kind = DSL_TOKEN_AND_AND;
    } else if (lexer->token.length == 2u &&
               strncmp(lexer->token.start, "or", 2u) == 0) {
      lexer->token.kind = DSL_TOKEN_OR_OR;
    } else if (lexer->token.length == 3u &&
               strncmp(lexer->token.start, "not", 3u) == 0) {
      lexer->token.kind = DSL_TOKEN_BANG;
    }
    return;
  }
  if (current == '"') {
    DebugDslLexerString(lexer, start, column);
    return;
  }
  switch (current) {
  case '(':
    lexer->token.kind = DSL_TOKEN_LEFT_PAREN;
    return;
  case ')':
    lexer->token.kind = DSL_TOKEN_RIGHT_PAREN;
    return;
  case '[':
    lexer->token.kind = DSL_TOKEN_LEFT_BRACKET;
    return;
  case ']':
    lexer->token.kind = DSL_TOKEN_RIGHT_BRACKET;
    return;
  case ',':
    lexer->token.kind = DSL_TOKEN_COMMA;
    return;
  case '.':
    lexer->token.kind = DSL_TOKEN_DOT;
    return;
  case '+':
    lexer->token.kind = DSL_TOKEN_PLUS;
    return;
  case '-':
    lexer->token.kind = DSL_TOKEN_MINUS;
    return;
  case '*':
    lexer->token.kind = DSL_TOKEN_STAR;
    return;
  case '/':
    lexer->token.kind = DSL_TOKEN_SLASH;
    return;
  case '%':
    lexer->token.kind = DSL_TOKEN_PERCENT;
    return;
  case '!':
    lexer->token.kind = lexer->source[lexer->offset] == '='
                            ? DSL_TOKEN_BANG_EQUAL
                            : DSL_TOKEN_BANG;
    if (lexer->token.kind == DSL_TOKEN_BANG_EQUAL)
      lexer->offset++;
    lexer->token.length = lexer->offset - start;
    return;
  case '=':
    if (lexer->source[lexer->offset] == '=') {
      lexer->offset++;
      lexer->token.kind = DSL_TOKEN_EQUAL_EQUAL;
      lexer->token.length = 2u;
      return;
    }
    break;
  case '<':
    lexer->token.kind = lexer->source[lexer->offset] == '='
                            ? DSL_TOKEN_LESS_EQUAL
                            : DSL_TOKEN_LESS;
    if (lexer->token.kind == DSL_TOKEN_LESS_EQUAL)
      lexer->offset++;
    lexer->token.length = lexer->offset - start;
    return;
  case '>':
    lexer->token.kind = lexer->source[lexer->offset] == '='
                            ? DSL_TOKEN_GREATER_EQUAL
                            : DSL_TOKEN_GREATER;
    if (lexer->token.kind == DSL_TOKEN_GREATER_EQUAL)
      lexer->offset++;
    lexer->token.length = lexer->offset - start;
    return;
  case '&':
    if (lexer->source[lexer->offset] == '&') {
      lexer->offset++;
      lexer->token.kind = DSL_TOKEN_AND_AND;
      lexer->token.length = 2u;
      return;
    }
    break;
  case '|':
    if (lexer->source[lexer->offset] == '|') {
      lexer->offset++;
      lexer->token.kind = DSL_TOKEN_OR_OR;
      lexer->token.length = 2u;
      return;
    }
    break;
  }
  DebugDslSetError(lexer->error, DEBUG_DSL_ERROR_SYNTAX, lexer->line, column,
                   "unexpected character '%c'", current);
  lexer->failed = true;
}

static bool DebugDslTokenIs(DebugDslExpressionParser *parser,
                            DebugDslTokenKind kind) {
  return parser->lexer.token.kind == kind;
}

static bool DebugDslAccept(DebugDslExpressionParser *parser,
                           DebugDslTokenKind kind) {
  if (!DebugDslTokenIs(parser, kind))
    return false;
  DebugDslLexerNext(&parser->lexer);
  return true;
}

static bool DebugDslExpect(DebugDslExpressionParser *parser,
                           DebugDslTokenKind kind, const char *description) {
  if (DebugDslAccept(parser, kind))
    return true;
  DebugDslSetError(parser->lexer.error, DEBUG_DSL_ERROR_SYNTAX,
                   parser->lexer.line, parser->lexer.token.column,
                   "expected %s", description);
  return false;
}

static DebugDslExpression *DebugDslParseOr(DebugDslExpressionParser *parser);

static bool DebugDslEnterExpressionNesting(DebugDslExpressionParser *parser,
                                           size_t column) {
  if (parser->nesting >= DEBUG_DSL_MAX_EXPRESSION_DEPTH) {
    DebugDslSetError(
        parser->lexer.error, DEBUG_DSL_ERROR_LIMIT, parser->lexer.line, column,
        "expression nesting exceeds %u", DEBUG_DSL_MAX_EXPRESSION_DEPTH);
    return false;
  }
  parser->nesting++;
  return true;
}

static DebugDslExpression *DebugDslParseVec3(
    DebugDslExpressionParser *parser, DebugDslToken token,
    DebugDslTokenKind closing, const char *closingName) {
  if (!DebugDslEnterExpressionNesting(parser, token.column))
    return NULL;
  DebugDslExpression *expression = DebugDslExpressionCreate(
      DSL_EXPR_VEC3, parser->lexer.line, token.column, parser->lexer.error);
  if (!expression) {
    parser->nesting--;
    return NULL;
  }
  for (int index = 0; index < 3; index++) {
    expression->as.components[index] = DebugDslParseOr(parser);
    if (!expression->as.components[index] ||
        (index < 2 && !DebugDslExpect(parser, DSL_TOKEN_COMMA, "','"))) {
      parser->nesting--;
      DebugDslExpressionDestroy(expression);
      return NULL;
    }
  }
  parser->nesting--;
  if (!DebugDslExpect(parser, closing, closingName)) {
    DebugDslExpressionDestroy(expression);
    return NULL;
  }
  return expression;
}

static DebugDslExpression *
DebugDslParsePrimary(DebugDslExpressionParser *parser) {
  DebugDslToken token = parser->lexer.token;
  if (DebugDslTokenIs(parser, DSL_TOKEN_NUMBER)) {
    DebugDslExpression *expression =
        DebugDslExpressionCreate(DSL_EXPR_LITERAL, parser->lexer.line,
                                 token.column, parser->lexer.error);
    if (!expression)
      return NULL;
    expression->as.literal = (DebugDslValue){.type = DEBUG_DSL_VALUE_NUMBER,
                                             .as.number = token.number};
    DebugDslLexerNext(&parser->lexer);
    return expression;
  }
  if (DebugDslTokenIs(parser, DSL_TOKEN_TRUE) ||
      DebugDslTokenIs(parser, DSL_TOKEN_FALSE)) {
    bool value = DebugDslTokenIs(parser, DSL_TOKEN_TRUE);
    DebugDslExpression *expression =
        DebugDslExpressionCreate(DSL_EXPR_LITERAL, parser->lexer.line,
                                 token.column, parser->lexer.error);
    if (!expression)
      return NULL;
    expression->as.literal =
        (DebugDslValue){.type = DEBUG_DSL_VALUE_BOOL, .as.boolean = value};
    DebugDslLexerNext(&parser->lexer);
    return expression;
  }
  if (DebugDslTokenIs(parser, DSL_TOKEN_STRING)) {
    DebugDslExpression *expression =
        DebugDslExpressionCreate(DSL_EXPR_LITERAL, parser->lexer.line,
                                 token.column, parser->lexer.error);
    if (!expression)
      return NULL;
    expression->as.literal.type = DEBUG_DSL_VALUE_STRING;
    expression->as.literal.as.string = parser->lexer.token.string;
    parser->lexer.token.string = NULL;
    DebugDslLexerNext(&parser->lexer);
    return expression;
  }
  if (DebugDslTokenIs(parser, DSL_TOKEN_IDENTIFIER)) {
    if (token.length == 4u && strncmp(token.start, "vec3", 4u) == 0) {
      DebugDslLexerNext(&parser->lexer);
      if (!DebugDslExpect(parser, DSL_TOKEN_LEFT_PAREN, "'('"))
        return NULL;
      return DebugDslParseVec3(
          parser, token, DSL_TOKEN_RIGHT_PAREN, "')'");
    }
    DebugDslExpression *expression =
        DebugDslExpressionCreate(DSL_EXPR_VARIABLE, parser->lexer.line,
                                 token.column, parser->lexer.error);
    if (!expression)
      return NULL;
    expression->as.variable = DebugDslDuplicateRange(token.start, token.length);
    if (!expression->as.variable) {
      DebugDslExpressionDestroy(expression);
      DebugDslSetError(parser->lexer.error, DEBUG_DSL_ERROR_ALLOCATION,
                       parser->lexer.line, token.column,
                       "out of memory while parsing variable");
      return NULL;
    }
    DebugDslLexerNext(&parser->lexer);
    while (DebugDslAccept(parser, DSL_TOKEN_DOT)) {
      DebugDslToken component = parser->lexer.token;
      if (!DebugDslTokenIs(parser, DSL_TOKEN_IDENTIFIER) ||
          component.length == 0u) {
        DebugDslExpressionDestroy(expression);
        DebugDslSetError(parser->lexer.error, DEBUG_DSL_ERROR_SYNTAX,
                         parser->lexer.line, component.column,
                         "invalid qualified name");
        return NULL;
      }
      if (expression->kind == DSL_EXPR_VARIABLE &&
          (component.length != 1u ||
           (component.start[0] != 'x' && component.start[0] != 'y' &&
            component.start[0] != 'z'))) {
        size_t oldLength = strlen(expression->as.variable);
        char *qualified = realloc(expression->as.variable,
                                  oldLength + 1u + component.length + 1u);
        if (!qualified) {
          DebugDslExpressionDestroy(expression);
          DebugDslSetError(parser->lexer.error, DEBUG_DSL_ERROR_ALLOCATION,
                           parser->lexer.line, component.column,
                           "out of memory while parsing name");
          return NULL;
        }
        qualified[oldLength] = '.';
        memcpy(qualified + oldLength + 1u, component.start, component.length);
        qualified[oldLength + 1u + component.length] = '\0';
        expression->as.variable = qualified;
        DebugDslLexerNext(&parser->lexer);
        continue;
      }
      if (component.length != 1u ||
          (component.start[0] != 'x' && component.start[0] != 'y' &&
           component.start[0] != 'z') ||
          expression->kind != DSL_EXPR_VARIABLE) {
        DebugDslExpressionDestroy(expression);
        DebugDslSetError(parser->lexer.error, DEBUG_DSL_ERROR_SYNTAX,
                         parser->lexer.line, component.column,
                         "vec3 field must be x, y, or z");
        return NULL;
      }
      DebugDslExpression *field =
          DebugDslExpressionCreate(DSL_EXPR_FIELD, parser->lexer.line,
                                   component.column, parser->lexer.error);
      if (!field) {
        DebugDslExpressionDestroy(expression);
        return NULL;
      }
      field->as.field.base = expression;
      field->as.field.component = component.start[0];
      expression = field;
      DebugDslLexerNext(&parser->lexer);
    }
    return expression;
  }
  if (DebugDslAccept(parser, DSL_TOKEN_LEFT_PAREN)) {
    if (!DebugDslEnterExpressionNesting(parser, token.column))
      return NULL;
    DebugDslExpression *expression = DebugDslParseOr(parser);
    parser->nesting--;
    if (!expression || !DebugDslExpect(parser, DSL_TOKEN_RIGHT_PAREN, "')'")) {
      DebugDslExpressionDestroy(expression);
      return NULL;
    }
    return expression;
  }
  if (DebugDslAccept(parser, DSL_TOKEN_LEFT_BRACKET)) {
    return DebugDslParseVec3(
        parser, token, DSL_TOKEN_RIGHT_BRACKET, "']'");
  }
  DebugDslSetError(parser->lexer.error, DEBUG_DSL_ERROR_SYNTAX,
                   parser->lexer.line, token.column, "expected expression");
  return NULL;
}

static DebugDslExpression *
DebugDslParseUnary(DebugDslExpressionParser *parser) {
  DebugDslTokenKind operation = parser->lexer.token.kind;
  size_t column = parser->lexer.token.column;
  if (operation != DSL_TOKEN_BANG && operation != DSL_TOKEN_MINUS &&
      operation != DSL_TOKEN_PLUS) {
    return DebugDslParsePrimary(parser);
  }
  if (!DebugDslEnterExpressionNesting(parser, column))
    return NULL;
  DebugDslLexerNext(&parser->lexer);
  DebugDslExpression *operand = DebugDslParseUnary(parser);
  parser->nesting--;
  if (!operand)
    return NULL;
  DebugDslExpression *expression = DebugDslExpressionCreate(
      DSL_EXPR_UNARY, parser->lexer.line, column, parser->lexer.error);
  if (!expression) {
    DebugDslExpressionDestroy(operand);
    return NULL;
  }
  expression->as.unary.operation = operation;
  expression->as.unary.operand = operand;
  return expression;
}

static DebugDslExpression *DebugDslMakeBinary(DebugDslExpressionParser *parser,
                                              DebugDslExpression *left,
                                              DebugDslTokenKind operation,
                                              size_t column,
                                              DebugDslExpression *right) {
  unsigned depth = left->depth > right->depth ? left->depth : right->depth;
  if (depth >= DEBUG_DSL_MAX_EXPRESSION_DEPTH) {
    DebugDslSetError(parser->lexer.error, DEBUG_DSL_ERROR_LIMIT,
                     parser->lexer.line, column,
                     "expression nesting exceeds %u",
                     DEBUG_DSL_MAX_EXPRESSION_DEPTH);
    DebugDslExpressionDestroy(left);
    DebugDslExpressionDestroy(right);
    return NULL;
  }
  DebugDslExpression *expression = DebugDslExpressionCreate(
      DSL_EXPR_BINARY, parser->lexer.line, column, parser->lexer.error);
  if (!expression) {
    DebugDslExpressionDestroy(left);
    DebugDslExpressionDestroy(right);
    return NULL;
  }
  expression->as.binary.operation = operation;
  expression->as.binary.left = left;
  expression->as.binary.right = right;
  expression->depth = depth + 1u;
  return expression;
}

#define DSL_PARSE_BINARY_FUNCTION(name, nextFunction, firstOp, secondOp)       \
  static DebugDslExpression *name(DebugDslExpressionParser *parser) {          \
    DebugDslExpression *left = nextFunction(parser);                           \
    if (!left)                                                                 \
      return NULL;                                                             \
    while (parser->lexer.token.kind == (firstOp) ||                            \
           parser->lexer.token.kind == (secondOp)) {                           \
      DebugDslTokenKind operation = parser->lexer.token.kind;                  \
      size_t column = parser->lexer.token.column;                              \
      DebugDslLexerNext(&parser->lexer);                                       \
      DebugDslExpression *right = nextFunction(parser);                        \
      if (!right) {                                                            \
        DebugDslExpressionDestroy(left);                                       \
        return NULL;                                                           \
      }                                                                        \
      left = DebugDslMakeBinary(parser, left, operation, column, right);       \
      if (!left)                                                               \
        return NULL;                                                           \
    }                                                                          \
    return left;                                                               \
  }

static DebugDslExpression *
DebugDslParseMultiplicative(DebugDslExpressionParser *parser) {
  DebugDslExpression *left = DebugDslParseUnary(parser);
  if (!left)
    return NULL;
  while (DebugDslTokenIs(parser, DSL_TOKEN_STAR) ||
         DebugDslTokenIs(parser, DSL_TOKEN_SLASH) ||
         DebugDslTokenIs(parser, DSL_TOKEN_PERCENT)) {
    DebugDslTokenKind operation = parser->lexer.token.kind;
    size_t column = parser->lexer.token.column;
    DebugDslLexerNext(&parser->lexer);
    DebugDslExpression *right = DebugDslParseUnary(parser);
    if (!right) {
      DebugDslExpressionDestroy(left);
      return NULL;
    }
    left = DebugDslMakeBinary(parser, left, operation, column, right);
    if (!left)
      return NULL;
  }
  return left;
}

DSL_PARSE_BINARY_FUNCTION(DebugDslParseAdditive, DebugDslParseMultiplicative,
                          DSL_TOKEN_PLUS, DSL_TOKEN_MINUS)

static DebugDslExpression *
DebugDslParseComparison(DebugDslExpressionParser *parser) {
  DebugDslExpression *left = DebugDslParseAdditive(parser);
  if (!left)
    return NULL;
  while (DebugDslTokenIs(parser, DSL_TOKEN_LESS) ||
         DebugDslTokenIs(parser, DSL_TOKEN_LESS_EQUAL) ||
         DebugDslTokenIs(parser, DSL_TOKEN_GREATER) ||
         DebugDslTokenIs(parser, DSL_TOKEN_GREATER_EQUAL)) {
    DebugDslTokenKind operation = parser->lexer.token.kind;
    size_t column = parser->lexer.token.column;
    DebugDslLexerNext(&parser->lexer);
    DebugDslExpression *right = DebugDslParseAdditive(parser);
    if (!right) {
      DebugDslExpressionDestroy(left);
      return NULL;
    }
    left = DebugDslMakeBinary(parser, left, operation, column, right);
    if (!left)
      return NULL;
  }
  return left;
}

DSL_PARSE_BINARY_FUNCTION(DebugDslParseEquality, DebugDslParseComparison,
                          DSL_TOKEN_EQUAL_EQUAL, DSL_TOKEN_BANG_EQUAL)
DSL_PARSE_BINARY_FUNCTION(DebugDslParseAnd, DebugDslParseEquality,
                          DSL_TOKEN_AND_AND, DSL_TOKEN_AND_AND)
DSL_PARSE_BINARY_FUNCTION(DebugDslParseOr, DebugDslParseAnd, DSL_TOKEN_OR_OR,
                          DSL_TOKEN_OR_OR)

#undef DSL_PARSE_BINARY_FUNCTION

static DebugDslExpression *DebugDslParseExpression(const char *text,
                                                   size_t line, size_t column,
                                                   DebugDslError *error) {
  DebugDslExpressionParser parser = {
      .lexer = {
          .source = text, .line = line, .baseColumn = column, .error = error}};
  DebugDslLexerNext(&parser.lexer);
  DebugDslExpression *expression = DebugDslParseOr(&parser);
  if (expression && !DebugDslTokenIs(&parser, DSL_TOKEN_END)) {
    DebugDslSetError(error, DEBUG_DSL_ERROR_SYNTAX, line,
                     parser.lexer.token.column,
                     "unexpected token after expression");
    DebugDslExpressionDestroy(expression);
    expression = NULL;
  }
  DebugDslTokenRelease(&parser.lexer.token);
  return expression;
}
static void DebugDslStatementDestroy(DebugDslStatement *statement) {
  while (statement) {
    DebugDslStatement *next = statement->next;
    switch (statement->kind) {
    case DSL_STATEMENT_LET:
      free(statement->as.let.name);
      DebugDslExpressionDestroy(statement->as.let.value);
      break;
    case DSL_STATEMENT_COMMAND:
      free(statement->as.command);
      break;
    case DSL_STATEMENT_ASSERT:
      DebugDslExpressionDestroy(statement->as.assertion);
      break;
    case DSL_STATEMENT_WAIT:
      DebugDslExpressionDestroy(statement->as.wait.condition);
      DebugDslExpressionDestroy(statement->as.wait.timeout);
      break;
    case DSL_STATEMENT_REPEAT:
      DebugDslExpressionDestroy(statement->as.repeat.count);
      DebugDslStatementDestroy(statement->as.repeat.body);
      break;
    case DSL_STATEMENT_EXIT:
      DebugDslExpressionDestroy(statement->as.exitCode);
      break;
    }
    free(statement);
    statement = next;
  }
}
void DebugDslScriptDestroy(DebugDslScript *script) {
  if (!script)
    return;
  DebugDslStatementDestroy(script->statements);
  free(script);
}
bool DebugDslScriptIsBatch(const DebugDslScript *script) {
  return script && script->batch;
}

static DebugDslStatement *DebugDslStatementCreate(DebugDslStatementKind kind,
                                                  size_t line, size_t column,
                                                  DebugDslError *error) {
  DebugDslStatement *statement = calloc(1u, sizeof(*statement));
  if (!statement) {
    DebugDslSetError(error, DEBUG_DSL_ERROR_ALLOCATION, line, column,
                     "out of memory while parsing statement");
    return NULL;
  }
  statement->kind = kind;
  statement->line = line;
  statement->column = column;
  return statement;
}
typedef struct DebugDslTextView {
  const char *text;
  size_t length;
  size_t column;
} DebugDslTextView;
static DebugDslTextView DebugDslTrimLine(DebugDslSourceLine line) {
  size_t start = 0u;
  while (start < line.length && isspace((unsigned char)line.text[start]))
    start++;
  size_t end = line.length;
  while (end > start && isspace((unsigned char)line.text[end - 1u]))
    end--;
  if (end > start && line.text[end - 1u] == ';')
    end--;
  return (DebugDslTextView){
      .text = line.text + start, .length = end - start, .column = start + 1u};
}
static bool DebugDslViewEquals(DebugDslTextView view, const char *text) {
  size_t length = strlen(text);
  return view.length == length && strncmp(view.text, text, length) == 0;
}

static bool DebugDslViewHasKeyword(DebugDslTextView view, const char *keyword,
                                   size_t *outAfter) {
  size_t length = strlen(keyword);
  if (view.length < length || strncmp(view.text, keyword, length) != 0) {
    return false;
  }
  if (view.length > length && !isspace((unsigned char)view.text[length]))
    return false;
  size_t after = length;
  while (after < view.length && isspace((unsigned char)view.text[after]))
    after++;
  if (outAfter)
    *outAfter = after;
  return true;
}

static char *DebugDslViewCopy(DebugDslTextView view, size_t start, size_t end,
                              size_t line, DebugDslError *error) {
  while (start < end && isspace((unsigned char)view.text[start]))
    start++;
  while (end > start && isspace((unsigned char)view.text[end - 1u]))
    end--;
  char *copy = DebugDslDuplicateRange(view.text + start, end - start);
  if (!copy) {
    DebugDslSetError(error, DEBUG_DSL_ERROR_ALLOCATION, line,
                     view.column + start,
                     "out of memory while parsing statement");
  }
  return copy;
}
static bool DebugDslIdentifierRange(const char *text, size_t length) {
  if (length == 0u || (!isalpha((unsigned char)text[0]) && text[0] != '_'))
    return false;
  for (size_t index = 1u; index < length; index++) {
    if (!isalnum((unsigned char)text[index]) && text[index] != '_') {
      return false;
    }
  }
  return true;
}
static bool DebugDslReservedIdentifier(const char *text, size_t length) {
  static const char *reserved[] = {
      "and", "assert", "exit", "false", "let", "not", "or", "repeat",
      "timeout", "true", "until", "vec3", "wait"};
  for (size_t index = 0u; index < sizeof(reserved) / sizeof(reserved[0]);
       index++) {
    if (strlen(reserved[index]) == length &&
        strncmp(text, reserved[index], length) == 0)
      return true;
  }
  return false;
}

static size_t DebugDslFindKeywordOutsideExpression(const char *text,
                                                   size_t length,
                                                   const char *keyword) {
  size_t keywordLength = strlen(keyword);
  int parenDepth = 0;
  int bracketDepth = 0;
  bool inString = false;
  bool escaped = false;
  for (size_t index = 0u; index + keywordLength <= length; index++) {
    char current = text[index];
    if (inString) {
      if (escaped)
        escaped = false;
      else if (current == '\\')
        escaped = true;
      else if (current == '"')
        inString = false;
      continue;
    }
    if (current == '"') {
      inString = true;
      continue;
    }
    if (current == '(')
      parenDepth++;
    else if (current == ')' && parenDepth > 0)
      parenDepth--;
    else if (current == '[')
      bracketDepth++;
    else if (current == ']' && bracketDepth > 0)
      bracketDepth--;
    if (parenDepth != 0 || bracketDepth != 0)
      continue;
    if (strncmp(text + index, keyword, keywordLength) != 0)
      continue;
    bool leftBoundary = index == 0u || isspace((unsigned char)text[index - 1u]);
    bool rightBoundary = index + keywordLength == length ||
                         isspace((unsigned char)text[index + keywordLength]);
    if (leftBoundary && rightBoundary)
      return index;
  }
  return (size_t)-1;
}

static bool DebugDslBuildLines(const char *source,
                               DebugDslSourceLine **outLines, size_t *outCount,
                               DebugDslError *error) {
  size_t count = 1u;
  for (const char *cursor = source; *cursor != '\0'; cursor++) {
    if (*cursor == '\n')
      count++;
  }
  if (count > SIZE_MAX / sizeof(DebugDslSourceLine)) {
    DebugDslSetError(error, DEBUG_DSL_ERROR_LIMIT, 1u, 1u,
                     "script has too many lines");
    return false;
  }
  DebugDslSourceLine *lines = calloc(count, sizeof(*lines));
  if (!lines) {
    DebugDslSetError(error, DEBUG_DSL_ERROR_ALLOCATION, 1u, 1u,
                     "out of memory while reading script");
    return false;
  }
  size_t lineIndex = 0u;
  const char *start = source;
  for (const char *cursor = source;; cursor++) {
    if (*cursor != '\n' && *cursor != '\0')
      continue;
    size_t length = (size_t)(cursor - start);
    if (length > 0u && start[length - 1u] == '\r')
      length--;
    size_t number = lineIndex + 1u;
    lines[lineIndex++] =
        (DebugDslSourceLine){.text = start, .length = length, .line = number};
    if (*cursor == '\0')
      break;
    start = cursor + 1;
  }
  *outLines = lines;
  *outCount = lineIndex;
  return true;
}

static DebugDslExpression *DebugDslParseViewExpression(DebugDslTextView view,
                                                       size_t start, size_t end,
                                                       size_t line,
                                                       DebugDslError *error) {
  char *text = DebugDslViewCopy(view, start, end, line, error);
  if (!text)
    return NULL;
  if (text[0] == '\0') {
    DebugDslSetError(error, DEBUG_DSL_ERROR_SYNTAX, line, view.column + start,
                     "expected expression");
    free(text);
    return NULL;
  }
  size_t leading = start;
  while (leading < end && isspace((unsigned char)view.text[leading])) {
    leading++;
  }
  DebugDslExpression *expression =
      DebugDslParseExpression(text, line, view.column + leading, error);
  free(text);
  return expression;
}

static DebugDslStatement *DebugDslParseBlock(DebugDslParser *parser,
                                             unsigned depth, bool nested,
                                             bool *outClosed);

static DebugDslStatement *DebugDslParseLet(DebugDslParser *parser,
                                           DebugDslTextView view, size_t line,
                                           size_t after) {
  size_t equals = after;
  while (equals < view.length && view.text[equals] != '=')
    equals++;
  if (equals == view.length) {
    DebugDslSetError(parser->error, DEBUG_DSL_ERROR_SYNTAX, line,
                     view.column + after, "let statement requires '='");
    return NULL;
  }
  size_t nameEnd = equals;
  while (nameEnd > after && isspace((unsigned char)view.text[nameEnd - 1u]))
    nameEnd--;
  if (!DebugDslIdentifierRange(view.text + after, nameEnd - after) ||
      DebugDslReservedIdentifier(view.text + after, nameEnd - after)) {
    DebugDslSetError(parser->error, DEBUG_DSL_ERROR_SYNTAX, line,
                     view.column + after, "invalid variable name");
    return NULL;
  }
  DebugDslStatement *statement = DebugDslStatementCreate(
      DSL_STATEMENT_LET, line, view.column, parser->error);
  if (!statement)
    return NULL;
  statement->as.let.name =
      DebugDslDuplicateRange(view.text + after, nameEnd - after);
  statement->as.let.value = DebugDslParseViewExpression(
      view, equals + 1u, view.length, line, parser->error);
  if (!statement->as.let.name || !statement->as.let.value) {
    if (!statement->as.let.name) {
      DebugDslSetError(parser->error, DEBUG_DSL_ERROR_ALLOCATION, line,
                       view.column + after,
                       "out of memory while parsing variable");
    }
    DebugDslStatementDestroy(statement);
    return NULL;
  }
  return statement;
}

static DebugDslStatement *DebugDslParseWait(DebugDslParser *parser,
                                            DebugDslTextView view, size_t line,
                                            size_t afterUntil) {
  size_t timeout = DebugDslFindKeywordOutsideExpression(
      view.text + afterUntil, view.length - afterUntil, "timeout");
  if (timeout == (size_t)-1) {
    DebugDslSetError(parser->error, DEBUG_DSL_ERROR_SYNTAX, line,
                     view.column + afterUntil,
                     "wait until requires a timeout expression");
    return NULL;
  }
  timeout += afterUntil;
  DebugDslStatement *statement = DebugDslStatementCreate(
      DSL_STATEMENT_WAIT, line, view.column, parser->error);
  if (!statement)
    return NULL;
  statement->as.wait.condition = DebugDslParseViewExpression(
      view, afterUntil, timeout, line, parser->error);
  statement->as.wait.timeout = DebugDslParseViewExpression(
      view, timeout + strlen("timeout"), view.length, line, parser->error);
  if (!statement->as.wait.condition || !statement->as.wait.timeout) {
    DebugDslStatementDestroy(statement);
    return NULL;
  }
  return statement;
}

static DebugDslStatement *DebugDslParseRepeat(DebugDslParser *parser,
                                              DebugDslTextView view,
                                              size_t line, size_t after,
                                              unsigned depth) {
  if (depth >= DEBUG_DSL_MAX_REPEAT_DEPTH) {
    DebugDslSetError(parser->error, DEBUG_DSL_ERROR_LIMIT, line, view.column,
                     "repeat nesting exceeds %u", DEBUG_DSL_MAX_REPEAT_DEPTH);
    return NULL;
  }
  if (view.length <= after || view.text[view.length - 1u] != '{') {
    DebugDslSetError(parser->error, DEBUG_DSL_ERROR_SYNTAX, line,
                     view.column + view.length,
                     "repeat statement must end with '{'");
    return NULL;
  }
  DebugDslStatement *statement = DebugDslStatementCreate(
      DSL_STATEMENT_REPEAT, line, view.column, parser->error);
  if (!statement)
    return NULL;
  statement->as.repeat.count = DebugDslParseViewExpression(
      view, after, view.length - 1u, line, parser->error);
  if (!statement->as.repeat.count) {
    DebugDslStatementDestroy(statement);
    return NULL;
  }
  bool closed = false;
  statement->as.repeat.body =
      DebugDslParseBlock(parser, depth + 1u, true, &closed);
  if (parser->error->code != DEBUG_DSL_ERROR_NONE || !closed) {
    if (parser->error->code == DEBUG_DSL_ERROR_NONE) {
      DebugDslSetError(parser->error, DEBUG_DSL_ERROR_SYNTAX, line, view.column,
                       "unterminated repeat block");
    }
    DebugDslStatementDestroy(statement);
    return NULL;
  }
  return statement;
}

static DebugDslStatement *DebugDslParseBlock(DebugDslParser *parser,
                                             unsigned depth, bool nested,
                                             bool *outClosed) {
  DebugDslStatement *head = NULL;
  DebugDslStatement **tail = &head;
  bool seenExit = false;
  if (outClosed)
    *outClosed = false;
  while (parser->index < parser->lineCount) {
    DebugDslSourceLine sourceLine = parser->lines[parser->index++];
    DebugDslTextView view = DebugDslTrimLine(sourceLine);
    if (view.length == 0u || view.text[0] == '#')
      continue;
    if (DebugDslViewEquals(view, "}")) {
      if (!nested) {
        DebugDslSetError(parser->error, DEBUG_DSL_ERROR_SYNTAX, sourceLine.line,
                         view.column, "unexpected '}'");
        break;
      }
      if (outClosed)
        *outClosed = true;
      return head;
    }
    if (seenExit) {
      DebugDslSetError(parser->error, DEBUG_DSL_ERROR_SYNTAX, sourceLine.line,
                       view.column,
                       "exit must be the final top-level statement");
      break;
    }

    size_t after = 0u;
    DebugDslStatement *statement = NULL;
    if (DebugDslViewHasKeyword(view, "let", &after)) {
      statement = DebugDslParseLet(parser, view, sourceLine.line, after);
    } else if (DebugDslViewHasKeyword(view, "assert", &after)) {
      statement = DebugDslStatementCreate(DSL_STATEMENT_ASSERT, sourceLine.line,
                                          view.column, parser->error);
      if (statement) {
        statement->as.assertion = DebugDslParseViewExpression(
            view, after, view.length, sourceLine.line, parser->error);
        if (!statement->as.assertion) {
          DebugDslStatementDestroy(statement);
          statement = NULL;
        }
      }
    } else if (DebugDslViewHasKeyword(view, "wait", &after)) {
      DebugDslTextView remainder = {.text = view.text + after,
                                    .length = view.length - after,
                                    .column = view.column + after};
      size_t afterUntil = 0u;
      if (!DebugDslViewHasKeyword(remainder, "until", &afterUntil)) {
        DebugDslSetError(parser->error, DEBUG_DSL_ERROR_SYNTAX, sourceLine.line,
                         remainder.column, "wait statement requires 'until'");
      } else {
        statement =
            DebugDslParseWait(parser, remainder, sourceLine.line, afterUntil);
      }
    } else if (DebugDslViewHasKeyword(view, "repeat", &after)) {
      statement =
          DebugDslParseRepeat(parser, view, sourceLine.line, after, depth);
    } else if (DebugDslViewHasKeyword(view, "exit", &after)) {
      if (nested) {
        DebugDslSetError(parser->error, DEBUG_DSL_ERROR_SYNTAX, sourceLine.line,
                         view.column, "exit is only valid at top level");
      } else {
        statement = DebugDslStatementCreate(DSL_STATEMENT_EXIT, sourceLine.line,
                                            view.column, parser->error);
        if (statement && after < view.length) {
          statement->as.exitCode = DebugDslParseViewExpression(
              view, after, view.length, sourceLine.line, parser->error);
        } else if (statement) {
          statement->as.exitCode = DebugDslExpressionCreate(
              DSL_EXPR_LITERAL, sourceLine.line, view.column, parser->error);
          if (statement->as.exitCode) {
            statement->as.exitCode->as.literal = (DebugDslValue){
                .type = DEBUG_DSL_VALUE_NUMBER, .as.number = 0.0};
          }
        }
        if (statement && !statement->as.exitCode) {
          DebugDslStatementDestroy(statement);
          statement = NULL;
        }
        seenExit = statement != NULL;
      }
    } else {
      statement = DebugDslStatementCreate(
          DSL_STATEMENT_COMMAND, sourceLine.line, view.column, parser->error);
      if (statement) {
        statement->as.command = DebugDslDuplicateRange(view.text, view.length);
        if (!statement->as.command) {
          DebugDslSetError(parser->error, DEBUG_DSL_ERROR_ALLOCATION,
                           sourceLine.line, view.column,
                           "out of memory while parsing command");
          DebugDslStatementDestroy(statement);
          statement = NULL;
        }
      }
    }
    if (!statement || parser->error->code != DEBUG_DSL_ERROR_NONE)
      break;
    *tail = statement;
    tail = &statement->next;
  }
  if (parser->error->code != DEBUG_DSL_ERROR_NONE) {
    DebugDslStatementDestroy(head);
    return NULL;
  }
  if (nested && outClosed && !*outClosed) {
    DebugDslStatementDestroy(head);
    return NULL;
  }
  return head;
}

bool DebugDslParse(const char *source, DebugDslScript **outScript,
                   DebugDslError *outError) {
  if (outScript)
    *outScript = NULL;
  DebugDslErrorClear(outError);
  if (!source || !outScript || !outError) {
    DebugDslSetError(outError, DEBUG_DSL_ERROR_ARGUMENT, 0u, 0u,
                     "source, output script, and error are required");
    return false;
  }
  if (strlen(source) > DEBUG_DSL_MAX_SOURCE_BYTES) {
    DebugDslSetError(outError, DEBUG_DSL_ERROR_LIMIT, 1u, 1u,
                     "script exceeds %u bytes", DEBUG_DSL_MAX_SOURCE_BYTES);
    return false;
  }
  DebugDslParser parser = {.error = outError};
  if (!DebugDslBuildLines(source, &parser.lines, &parser.lineCount, outError))
    return false;
  DebugDslScript *script = calloc(1u, sizeof(*script));
  if (!script) {
    free(parser.lines);
    DebugDslSetError(outError, DEBUG_DSL_ERROR_ALLOCATION, 1u, 1u,
                     "out of memory while parsing script");
    return false;
  }
  bool closed = false;
  script->statements = DebugDslParseBlock(&parser, 0u, false, &closed);
  free(parser.lines);
  if (outError->code != DEBUG_DSL_ERROR_NONE) {
    DebugDslScriptDestroy(script);
    return false;
  }
  DebugDslStatement *last = script->statements;
  while (last && last->next)
    last = last->next;
  script->batch = last && last->kind == DSL_STATEMENT_EXIT;
  *outScript = script;
  return true;
}
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
