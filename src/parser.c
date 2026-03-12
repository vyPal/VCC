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
  } else if (*text == '+' || *text == '*' || *text == '/' || *text == '%') {
    (*len)++;
    return 3;

  } else if (*text == '(' || *text == ')' || *text == '{' || *text == '}' ||
             *text == ';' || *text == '=' || *text == ',') {
    (*len)++;
    return 4;
  } else
    return 5;
}

void advance(parser_state *s) {
  if (s->next_start == NULL) {
    s->src += s->current_len;
    skip_whitespace(&s->src);
    s->current_kind = determine_kind(s->src, &s->current_len);
  } else {
    s->src = s->next_start;
    s->current_kind = s->next_kind;
    s->current_len = s->next_len;
  }
  s->next_start = s->src + s->current_len;
  skip_whitespace(&s->next_start);
  s->next_kind = determine_kind(s->next_start, &s->next_len);
}

ast_node *parse_primary(parser_state *s) {
  if (s->current_kind == 1 && s->next_kind == 4 &&
      strncmp("(", s->next_start, 1) == 0) {
    ast_node *node = malloc(sizeof(ast_node));
    if (node == NULL) {
      printf("Failed to allocate space for function call\n");
      return NULL;
    }
    node->type = CALL;
    ast_node_call *call = malloc(sizeof(ast_node_call));
    if (call == NULL) {
      printf("Failed to allocate space for function call value\n");
      free(node);
      return NULL;
    }
    call->name.ptr = s->src;
    call->name.len = s->current_len;
    advance(s); // name
    advance(s); // (
    call->argc = 0;
    call->args = NULL;
    while (s->current_kind == 1) {
      call->argc++;

      ast_node **args = realloc(call->args, sizeof(ast_node *) * call->argc);
      if (args == NULL) {
        if (call->args != NULL)
          free(call->args);
        free(call);
        free(node);
        printf("Failed to reallocate space for argument list\n");
        return NULL;
      }
      call->args = args;

      call->args[call->argc - 1] = parse_operator(s);

      if (s->current_kind == 4 && *s->src == ',') {
        advance(s); // ,
      } else
        break;
    }
    if (!(s->current_kind == 4 && *s->src == ')')) {
      free(call->args);
      free(call);
      free(node);
      printf("Expected end of arguments list (`)`), found %.*s\n",
             s->current_len, s->src);
      return NULL;
    }
    advance(s); // )
    node->node = call;
    return node;
  } else if (s->current_kind == 1 || s->current_kind == 2) {
    ast_node *node = malloc(sizeof(ast_node));
    if (node == NULL) {
      printf("Failed to allocate space for leaf node\n");
      return NULL;
    }
    node->type = LEAF;
    token_slice *slice = malloc(sizeof(token_slice));
    if (slice == NULL) {
      printf("Failed to allocate space for leaf node value\n");
      free(node);
      return NULL;
    }
    slice->ptr = s->src;
    slice->len = s->current_len;
    node->node = slice;
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
    new->node = op;
    left = new;
  }

  return left;
}

ast_node *parse_statement(parser_state *s, ast_node_function *parent_func) {
  ast_node *node = malloc(sizeof(ast_node));
  if (node == NULL) {
    printf("Failed to allocate space for node\n");
    return NULL;
  }

  if (s->current_len == 6 && strncmp(s->src, "return", 6) == 0) {
    node->type = RETURN;
    advance(s); // return
    if (s->current_kind == 4) {
      node->node = NULL;
    } else {
      node->node = parse_operator(s);
    }
  } else if (s->next_kind == 4) {
    if (strncmp("(", s->next_start, 1) == 0) {
      free(node);
      node = parse_primary(s);
    } else if (strncmp("=", s->next_start, 1) == 0) {
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
        printf("Expected assignment (`=`), found %.*s\n", s->current_len,
               s->src);
        return NULL;
      }
      advance(s); // =
      assign->value = parse_operator(s);
      node->node = assign;
      if (assign->value == NULL) {
        free_node(node);
        return NULL;
      }
    } else {
      printf("Unexpected character: `%.*s`\n", s->next_len, s->next_start);
      return NULL;
    }
  } else if (s->next_kind == 1) {
    node->type = VARIABLE;
    parent_func->localc++;
    ast_node_variable *variable = malloc(sizeof(ast_node_variable));
    if (variable == NULL) {
      free(node);
      printf("Failed to allocate space for node value\n");
      return NULL;
    }

    variable->name.ptr = s->next_start;
    variable->name.len = s->next_len;

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

  func->name.ptr = s->next_start;
  func->name.len = s->next_len;

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
  func->arg_names = NULL;
  func->arg_types = NULL;
  while (s->current_kind == 1) {
    func->argc++;

    token_slice *names =
        realloc(func->arg_names, sizeof(token_slice) * func->argc);
    if (names == NULL) {
      if (func->arg_names != NULL)
        free(func->arg_names);
      if (func->arg_types != NULL)
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
      if (func->arg_names != NULL)
        free(func->arg_names);
      if (func->arg_types != NULL)
        free(func->arg_types);
      free(func);
      free(node);
      printf("Failed to reallocate space for argument list\n");
      return NULL;
    }
    func->arg_types = types;

    func->arg_names[func->argc - 1].ptr = s->next_start;
    func->arg_names[func->argc - 1].len = s->next_len;
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
  func->nodec = 0;
  func->localc = 0;
  func->nodes = NULL;
  while (s->current_kind != 4) {
    func->nodec++;

    ast_node **nodes = realloc(func->nodes, sizeof(ast_node *) * func->nodec);
    if (nodes == NULL) {
      for (int i = 0; i < func->nodec - 1; i++) {
        free_node(func->nodes[i]);
      }
      if (func->nodes != NULL)
        free(func->nodes);
      free(func->arg_names);
      free(func->arg_types);
      free(func);
      free(node);
      printf("Failed to allocate space for function body\n");
      return NULL;
    }
    func->nodes = nodes;
    func->nodes[func->nodec - 1] = parse_statement(s, func);
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

    ast_node **new = realloc(*nodes, sizeof(ast_node *) * nodec);
    if (new == NULL) {
      for (int i = 0; i < nodec - 2; i++) {
        free_node(*nodes[i]);
      }
      if (*nodes != NULL)
        free(*nodes);
      printf("Failed to reallocate space for nodes\n");
      return 0;
    }
    *nodes = new;
    (*nodes)[nodec - 1] = parse_function(&state);
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
    if (f->nodec > 0)
      free(f->nodes);
    if (f->argc > 0) {
      free(f->arg_names);
      free(f->arg_types);
    }
    free(f);
    break;
  case VARIABLE:;
    ast_node_variable *v = (ast_node_variable *)node->node;
    free_node(v->initializer);
    free(v);
    break;
  case ASSIGNMENT:;
    ast_node_assignment *a = (ast_node_assignment *)node->node;
    free_node(a->value);
    free(a);
    break;
  case BINARY_OP:;
    ast_node_binary_op *b = (ast_node_binary_op *)node->node;
    free_node(b->left);
    free_node(b->right);
    free(b);
    break;
  case RETURN:
    free_node((ast_node *)node->node);
    break;
  case LEAF:
    free((token_slice *)node->node);
    break;
  case CALL:;
    ast_node_call *c = (ast_node_call *)node->node;
    for (int i = 0; i < c->argc; i++) {
      free_node(c->args[i]);
    }
    if (c->argc > 0)
      free(c->args);
    free(c);
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
    printf("Binary Op:\n");
    traverse_tree(b->left, level + 1);
    printf("%*s", level * 2, "");
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
  case LEAF:;
    token_slice *s = ((token_slice *)node->node);
    printf("Leaf | `%.*s`\n", s->len, s->ptr);
    break;
  case CALL:;
    ast_node_call *c = (ast_node_call *)node->node;
    printf("Call | `%.*s` args:\n", c->name.len, c->name.ptr);
    for (int i = 0; i < c->argc; i++) {
      traverse_tree(c->args[i], level + 1);
    }
  }
  if (level == 0)
    printf("\n");
}
