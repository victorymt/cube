#include "core/debug_dsl_internal.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
char *DebugDslDuplicateRange(const char *text, size_t length) {
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
void DebugDslExpressionDestroy(DebugDslExpression *expression) {
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

DebugDslExpression *DebugDslParseExpression(const char *text,
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
