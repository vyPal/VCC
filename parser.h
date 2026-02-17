#ifndef PARSER_H

typedef enum {
  FUNCTION,
  VARIABLE,
  ASSIGNMENT,
  BINARY_OP,
  RETURN,
  LEAF,
} ast_node_type;

typedef struct {
  ast_node_type type;
  void *node;
} ast_node;

// specific AST node structs

typedef struct {
  char *name;
  int argc;
  char **arg_names;
  char **arg_types;
  char *ret_type;
  int nodec;
  ast_node **nodes; // Pointer to array of `ast_node`s
} ast_node_function;

typedef struct {
  char *name;
  char *type;
  ast_node *initializer; // Pointer to optional initializer
} ast_node_variable;

typedef struct {
  char *name;
  ast_node *value;
} ast_node_assignment;

typedef struct {
  ast_node *left;
  ast_node *right;
  char *op;
} ast_node_binary_op;

typedef ast_node *ast_node_return; // Pointer to optional return value
typedef char *ast_node_leaf;

void skip_whitespace(char **text);
int determine_kind(char *text, int *len);
int parse_text(char *text, ast_node ***nodes);

void free_node(ast_node *node);
void traverse_tree(ast_node *node, int level);

#endif // !PARSER_H
