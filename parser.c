#include "parser.h"
#include "stdio.h"

void skip_whitespace(char **text) {
  while (**text == ' ' || **text == '\t' || **text == '\r' || **text == '\n' ||
         **text == '\v' || **text == '\f')
    (*text)++;
}

// Reads chars from text until the next token kind is determined
//
// Expects next character to not be whitespace
//
// Token kinds:
//  1. identifier - starts with letter and contains letters, numbers and
//  underscores
//  2. immediate (number) - any number, optionally prefixed by minus sign
//  3. operator - any accepted operator
//  4. special - special characters indicative of parser state change (like '{',
//  ';', etc.)
//  5. other - any other character that is not used in the syntax (probably an
//  error)
int determine_kind(char *text, int *len) {
  *len = 0;
  if ((*text >= 'a' && *text <= 'z') || (*text >= 'A' && *text <= 'Z') ||
      *text == '_') {
    (*len)++;
    while ((*(++text) >= 'a' && *text <= 'z') ||
           (*text >= 'A' && *text <= 'Z') || (*text >= '0' && *text <= '9') ||
           *text == '_')
      (*len)++;
    return 1;
  } else if (*text == '-') {
    (*len)++;
    while (*(++text) >= '0' && *text <= '9')
      (*len)++;
    return *len == 1 ? 3 : 2;
  } else if (*text >= '0' && *text <= '9') {
    (*len)++;
    while (*(++text) >= '0' && *text <= '9')
      (*len)++;
    return 2;
  } else if (*text == '+') {
    (*len)++;
    return 3;

  } else if (*text == '(' || *text == ')' || *text == '{' || *text == '}' ||
             *text == ';' || *text == '=' || *text == ',') {
    (*len)++;
    return 4;
  } else
    return 5;
}

typedef struct {
  char *src;
  int current_kind;
  int current_len;
} parser_state;

void advance(parser_state *s) {
  s->src += s->current_len;
  skip_whitespace(&s->src);
  s->current_kind = determine_kind(s->src, &s->current_len);
}

// Returns the type of the next token without modifying parser state
int peek(parser_state *s, int *next_len, char **next_start) {
  *next_start = s->src + s->current_len;
  skip_whitespace(next_start);
  return determine_kind(*next_start, next_len);
}

void parse_primary(parser_state *s) {
  if (s->current_kind == 1 || s->current_kind == 2) {
    printf("Found leaf: `%.*s`\n", s->current_len, s->src);
    advance(s);
  } else {
    printf("Expected expression\n");
  }
}

void parse_operator(parser_state *s) {
  parse_primary(s);
  while (s->current_kind == 3) {
    printf("With op: `%.*s`\n", s->current_len, s->src);
    advance(s);
    parse_primary(s);
  }
}

void parse_statement(parser_state *s) {
  int next_len;
  char *next_start;
  int next_type = peek(s, &next_len, &next_start);

  if (next_type == 4) {
    printf("Assignment to variable of name: `%.*s`\n", s->current_len, s->src);
    advance(s); // name
    if (!(s->current_kind == 4 && *s->src == '=')) {
      printf("Expected assignment (`=`), found %.*s\n", s->current_len, s->src);
      return;
    }
    advance(s); // =
    parse_operator(s);
  } else if (next_type == 1) {
    printf("Variable declaration of type `%.*s` with name `%.*s`\n",
           s->current_len, s->src, next_len, next_start);
    advance(s); // type
    advance(s); // name
    if (!(s->current_kind == 4 && *s->src == '=')) {
      printf("Expected assignment (`=`), found %.*s\n", s->current_len, s->src);
      return;
    }
    advance(s); // =
    parse_operator(s);
  }
  if (!(s->current_kind == 4 && *s->src == ';')) {
    printf("Expected end of statement (`;`), found %.*s\n", s->current_len,
           s->src);
    return;
  }
  advance(s); // ;
}

void parse_function(parser_state *s) {
  int next_len;
  char *next_start;
  int next_type = peek(s, &next_len, &next_start);
  printf("Function definition of type `%.*s` and name `%.*s`, arguments: (",
         s->current_len, s->src, next_len, next_start);
  advance(s); // type
  advance(s); // name
  if (!(s->current_kind == 4 && *s->src == '(')) {
    printf("Expected start of arguments list (`(`), found %.*s\n",
           s->current_len, s->src);
    return;
  }
  advance(s); // (
  while (s->current_kind == 1) {
    next_type = peek(s, &next_len, &next_start);
    printf("`%.*s`: `%.*s`, ", s->current_len, s->src, next_len, next_start);
    advance(s); // type
    advance(s); // name
    if (s->current_kind == 4 && *s->src == ',') {
      advance(s); // ,
    } else
      break;
  }
  printf(")\n");
  if (!(s->current_kind == 4 && *s->src == ')')) {
    printf("Expected end of arguments list (`)`), found %.*s\n", s->current_len,
           s->src);
    return;
  }
  advance(s); // )
  advance(s); // TODO: Verify `{`
  while (s->current_kind != 4) {
    parse_statement(s);
  }
  advance(s); // TODO: Verify `}`
}

// Parses a input file into an AST
//
// Returns value < 0 in case of error, otherwise returns number of nodes
//
// Nodes and their content must be freed by caller
int parse_text(char *text, ast_node **nodes) {
  skip_whitespace(&text);

  int nnodes = 0;

  parser_state state = {0};
  state.src = text;
  advance(&state);

  parse_function(&state);

  return nnodes;
}
