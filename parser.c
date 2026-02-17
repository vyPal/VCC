#include "parser.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

static inline void skip_whitespace(char **text) {
  while (1) {
    char c = **text;
    if (c != ' ' && c != '\n' && c != '\t' && c != '\r' && c != '\v' &&
        c != '\f')
      break;
    (*text)++;
  }
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

ast_node *parse_primary(parser_state *s) {
  if (s->current_kind == 1 || s->current_kind == 2) {
    ast_node *node = malloc(sizeof(ast_node));
    if (node == NULL) {
      printf("Failed to allocate space for leaf node\n");
      return NULL;
    }
    node->type = LEAF;
    node->node = calloc(s->current_len + 1, sizeof(char));
    if (node == NULL) {
      printf("Failed to allocate space for leaf node value\n");
      free(node);
      return NULL;
    }
    memcpy(node->node, s->src, s->current_len);
    advance(s);
    return node;
  } else {
    printf("Expected expression\n");
    return NULL;
  }
}

ast_node *parse_operator(parser_state *s) {
  ast_node *left = parse_primary(s);
  if (left == NULL) {
    return NULL;
  }

  while (s->current_kind == 3) {
    ast_node *new = malloc(sizeof(ast_node));
    if (new == NULL) {
      free_node(left);
      printf("Failed to allocate space for operator node\n");
      return NULL;
    }
    new->type = BINARY_OP;

    ast_node_binary_op *op = malloc(sizeof(ast_node_binary_op));
    if (op == NULL) {
      free_node(left);
      free(new);
      printf("Failed to allocate space for operator node value\n");
      return NULL;
    }

    op->op.ptr = s->src;
    op->op.len = s->current_len;
    op->left = left;
    advance(s);
    op->right = parse_primary(s);
    if (op->right == NULL) {
      free_node(new);
      return NULL;
    }
    left = new;
  }

  return left;
}

ast_node *parse_statement(parser_state *s) {
  int next_len;
  char *next_start;
  int next_type = peek(s, &next_len, &next_start);

  ast_node *node = malloc(sizeof(ast_node));
  if (node == NULL) {
    printf("Failed to allocate space for node\n");
    return NULL;
  }

  if (s->current_len >= 6 && strncmp(s->src, "return", 6) == 0) {
    node->type = RETURN;
    advance(s);
    if (next_type == 4) {
      node->node = NULL;
    } else {
      node->node = parse_operator(s);
    }
  } else if (next_type == 4) {
    node->type = ASSIGNMENT;
    ast_node_assignment *assign = malloc(sizeof(ast_node_assignment));
    if (assign == NULL) {
      free(node);
      printf("Failed to allocate space for node value\n");
      return NULL;
    }
    assign->name.ptr = s->src;
    assign->name.len = s->current_len;
    advance(s); // name
    if (!(s->current_kind == 4 && *s->src == '=')) {
      free(assign);
      free(node);
      printf("Expected assignment (`=`), found %.*s\n", s->current_len, s->src);
      return NULL;
    }
    advance(s); // =
    assign->value = parse_operator(s);
    node->node = assign;
    if (assign->value == NULL) {
      free_node(node);
      return NULL;
    }
  } else if (next_type == 1) {
    node->type = VARIABLE;
    ast_node_variable *variable = malloc(sizeof(ast_node_variable));
    if (variable == NULL) {
      free(node);
      printf("Failed to allocate space for node value\n");
      return NULL;
    }
    variable->name.ptr = next_start;
    variable->name.len = next_len;

    variable->type.ptr = s->src;
    variable->type.len = s->current_len;
    advance(s); // type
    advance(s); // name
    if (!(s->current_kind == 4 && *s->src == '=')) {
      free(variable);
      free(node);
      printf("Expected assignment (`=`), found %.*s\n", s->current_len, s->src);
      return NULL;
    }
    advance(s); // =
    if (s->current_kind != 4) {
      variable->initializer = parse_operator(s);
    }
    node->node = variable;
  }

  if (!(s->current_kind == 4 && *s->src == ';')) {
    free_node(node);
    printf("Expected end of statement (`;`), found %.*s\n", s->current_len,
           s->src);
    return NULL;
  }
  advance(s); // ;
  return node;
}

ast_node *parse_function(parser_state *s) {
  ast_node *node = malloc(sizeof(ast_node));
  if (node == NULL) {
    printf("Failed to allocate space for node\n");
    return NULL;
  }
  node->type = FUNCTION;

  ast_node_function *func = malloc(sizeof(ast_node_function));
  if (func == NULL) {
    free(node);
    printf("Failed to allocate space for node value\n");
    return NULL;
  }

  int next_len;
  char *next_start;
  int next_type = peek(s, &next_len, &next_start);

  func->name.ptr = next_start;
  func->name.len = next_len;

  func->ret_type.ptr = s->src;
  func->ret_type.len = s->current_len;

  advance(s); // type
  advance(s); // name
  if (!(s->current_kind == 4 && *s->src == '(')) {
    free(func);
    free(node);
    printf("Expected start of arguments list (`(`), found %.*s\n",
           s->current_len, s->src);
    return NULL;
  }
  advance(s); // (
  func->argc = 0;
  while (s->current_kind == 1) {
    next_type = peek(s, &next_len, &next_start);

    func->argc++;
    if (func->argc == 1) {
      func->arg_names = malloc(sizeof(char *));
      if (func->arg_names == NULL) {
        free(func);
        free(node);
        printf("Failed to allocate space for argument list\n");
        return NULL;
      }
      func->arg_types = malloc(sizeof(char *));
      if (func->arg_names == NULL) {
        free(func->arg_names);
        free(func);
        free(node);
        printf("Failed to allocate space for argument list\n");
        return NULL;
      }
    } else {
      token_slice *names =
          realloc(func->arg_names, sizeof(token_slice) * func->argc);
      if (names == NULL) {
        free(func->arg_names);
        free(func->arg_types);
        free(func);
        free(node);
        printf("Failed to reallocate space for argument list\n");
        return NULL;
      }
      func->arg_names = names;
      token_slice *types =
          realloc(func->arg_types, sizeof(token_slice) * func->argc);
      if (types == NULL) {
        free(func->arg_names);
        free(func->arg_types);
        free(func);
        free(node);
        printf("Failed to reallocate space for argument list\n");
        return NULL;
      }
      func->arg_types = types;
    }

    func->arg_names[func->argc - 1].ptr = next_start;
    func->arg_names[func->argc - 1].len = next_len;
    func->arg_types[func->argc - 1].ptr = s->src;
    func->arg_types[func->argc - 1].len = s->current_len;

    advance(s); // type
    advance(s); // name
    if (s->current_kind == 4 && *s->src == ',') {
      advance(s); // ,
    } else
      break;
  }
  if (!(s->current_kind == 4 && *s->src == ')')) {
    free(func->arg_names);
    free(func->arg_types);
    free(func);
    free(node);
    printf("Expected end of arguments list (`)`), found %.*s\n", s->current_len,
           s->src);
    return NULL;
  }
  advance(s); // )
  if (!(s->current_kind == 4 && *s->src == '{')) {
    free(func->arg_names);
    free(func->arg_types);
    free(func);
    free(node);
    printf("Expected start of function block (`{`), found %.*s\n",
           s->current_len, s->src);
    return NULL;
  }
  advance(s); // {
  while (s->current_kind != 4) {
    func->nodec++;
    if (func->nodec == 1) {
      func->nodes = malloc(sizeof(ast_node *));
      if (func->nodes == NULL) {
        free(func->arg_names);
        free(func->arg_types);
        free(func);
        free(node);
        printf("Failed to allocate space for function body\n");
        return NULL;
      }
    } else {
      ast_node **nodes = realloc(func->nodes, sizeof(ast_node *) * func->nodec);
      if (nodes == NULL) {
        for (int i = 0; i < func->nodec - 1; i++) {
          free_node(func->nodes[i]);
        }
        free(func->nodes);
        free(func->arg_names);
        free(func->arg_types);
        free(func);
        free(node);
        printf("Failed to allocate space for function body\n");
        return NULL;
      }
      func->nodes = nodes;
    }
    func->nodes[func->nodec - 1] = parse_statement(s);
  }
  if (!(s->current_kind == 4 && *s->src == '}')) {
    for (int i = 0; i < func->nodec; i++) {
      free_node(func->nodes[i]);
    }
    free(func->nodes);
    free(func->arg_names);
    free(func->arg_types);
    free(func);
    free(node);
    printf("Expected end of function block (`}`), found %.*s\n", s->current_len,
           s->src);
    return NULL;
  }
  advance(s); // }

  node->node = func;
  return node;
}

// Parses a input file into an AST
//
// Returns value < 0 in case of error, otherwise returns number of nodes
//
// Nodes and their content must be freed by caller
int parse_text(char *text, ast_node ***nodes) {
  skip_whitespace(&text);

  int nodec = 0;

  parser_state state = {0};
  state.src = text;
  advance(&state);

  while (state.current_len != 0) {
    nodec++;
    if (nodec == 1) {
      *nodes = malloc(sizeof(ast_node *));
      if (*nodes == NULL) {
        printf("Failed to allocate space for nodes\n");
        return 0;
      }
    } else {
      ast_node **new = realloc(*nodes, sizeof(ast_node *) * nodec);
      if (new == NULL) {
        for (int i = 0; i < nodec - 2; i++) {
          free_node(*nodes[i]);
        }
        free(*nodes);
        printf("Failed to reallocate space for nodes\n");
        return 0;
      }
      *nodes = new;
    }
    *nodes[nodec - 1] = parse_function(&state);
  }

  return nodec;
}

void free_node(ast_node *node) {
  if (node == NULL)
    return;
  switch (node->type) {
  case FUNCTION:;
    ast_node_function *f = (ast_node_function *)node->node;
    for (int i = 0; i < f->nodec; i++) {
      free_node(f->nodes[i]);
    }
    break;
  case VARIABLE:;
    ast_node_variable *v = (ast_node_variable *)node->node;
    free_node(v->initializer);
    break;
  case ASSIGNMENT:;
    ast_node_assignment *a = (ast_node_assignment *)node->node;
    free_node(a->value);
    break;
  case BINARY_OP:;
    ast_node_binary_op *b = (ast_node_binary_op *)node->node;
    free_node(b->left);
    free_node(b->right);
    break;
  case RETURN:
    free_node((ast_node *)node->node);
    break;
  case LEAF:
    free((char *)node->node);
    break;
  }
  free(node);
}

void traverse_tree(ast_node *node, int level) {
  if (node == NULL) {
    printf("NULL NODE (ERROR)\n");
    return;
  }
  printf("%*s", level * 2, "");
  switch (node->type) {
  case FUNCTION:;
    ast_node_function *f = (ast_node_function *)node->node;
    printf("Function | name: `%.*s` return: `%.*s` args: ", f->name.len,
           f->name.ptr, f->ret_type.len, f->ret_type.ptr);
    for (int i = 0; i < f->argc; i++) {
      printf("`%.*s %.*s`", f->arg_types[i].len, f->arg_types[i].ptr,
             f->arg_names[i].len, f->arg_names[i].ptr);
      if (i != f->argc - 1)
        printf(", ");
    }
    printf(" | body:\n");
    for (int i = 0; i < f->nodec; i++) {
      traverse_tree(f->nodes[i], level + 1);
    }
    break;
  case VARIABLE:;
    ast_node_variable *v = (ast_node_variable *)node->node;
    printf("Variable | name: `%.*s` type: `%.*s`", v->name.len, v->name.ptr,
           v->type.len, v->type.ptr);
    if (v->initializer != NULL) {
      printf(" | initializer:\n");
      traverse_tree(v->initializer, level + 1);
    }
    break;
  case ASSIGNMENT:;
    ast_node_assignment *a = (ast_node_assignment *)node->node;
    printf("Assignment | name: `%.*s` | value:\n", a->name.len, a->name.ptr);
    traverse_tree(a->value, level + 1);
    break;
  case BINARY_OP:;
    ast_node_binary_op *b = (ast_node_binary_op *)node->node;
    printf("Binary Op:");
    traverse_tree(b->left, level + 1);
    printf("%*s", level, "");
    printf("%.*s\n", b->op.len, b->op.ptr);
    traverse_tree(b->right, level + 1);
    break;
  case RETURN:
    printf("Return");
    if (node->node != NULL) {
      printf(" value:\n");
      traverse_tree(node->node, level + 1);
    }
    break;
  case LEAF:
    printf("Leaf | `%s`\n", (char *)node->node);
  }
  if (level == 0)
    printf("\n");
}
