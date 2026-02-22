#ifndef COMPILER_H
#define COMPILER_H

#include "parser.h"

typedef struct {
  token_slice name;
  int offset;
} variable;

typedef struct {
  variable *variables;
  int variable_count;
  int current_stack_offset;
  token_slice func_name;
} variable_map;

typedef struct {
  char *generated;
  int output_len;
  variable_map *varmap;
  int varmapc;
} compiler_state;

int generate_node(compiler_state *state, ast_node *node, token_slice *parent);

int generate_asm(ast_node **source, int nodec, char **output);

#endif // !COMPILER_H
