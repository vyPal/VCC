#ifndef PARSER_H
#define PARSER_H

typedef struct {
  const char *ptr;
  int len;
} token_slice;

typedef enum {
  FUNCTION,
  VARIABLE,
  ASSIGNMENT,
  BINARY_OP,
  RETURN,
  LEAF,
  CALL,
} ast_node_type;

typedef struct {
  ast_node_type type;
  void *node;
} ast_node;

typedef struct {
  token_slice base;
  int pointer_depth;
} parsed_type;

// specific AST node structs

typedef struct {
  token_slice name;
  int argc;
  token_slice *arg_names;
  parsed_type *arg_types;
  parsed_type ret_type;
  int nodec;
  ast_node **nodes; // Pointer to array of `ast_node`s
  int localc;
} ast_node_function;

typedef struct {
  token_slice name;
  parsed_type type;
  ast_node *initializer; // Pointer to optional initializer
} ast_node_variable;

typedef struct {
  token_slice name;
  ast_node *value;
} ast_node_assignment;

typedef struct {
  ast_node *left;
  ast_node *right;
  token_slice op;
} ast_node_binary_op;

typedef ast_node *ast_node_return; // Pointer to optional return value
typedef token_slice ast_node_leaf;

typedef struct {
  token_slice name;
  ast_node **args;
  int argc;
} ast_node_call;

typedef struct {
  char *src;
  int current_kind;
  int current_len;
  char *next_start;
  int next_kind;
  int next_len;
} parser_state;

static inline void skip_whitespace(char **text) {
  while (1) {
    char c = **text;
    if (c != ' ' && c != '\n' && c != '\t' && c != '\r' && c != '\v' &&
        c != '\f')
      break;
    (*text)++;
  }
}

int determine_kind(char *text, int *len);

int parse_type(parser_state *state, parsed_type *out);

ast_node *parse_primary(parser_state *state);
ast_node *parse_addsub(parser_state *state);
ast_node *parse_muldiv(parser_state *state);
ast_node *parse_statement(parser_state *state, ast_node_function *parent_func);
ast_node *parse_function(parser_state *state);

int parse_text(char *text, ast_node ***nodes);

void free_node(ast_node *node);
void traverse_tree(ast_node *node, int level);

#endif // !PARSER_H
