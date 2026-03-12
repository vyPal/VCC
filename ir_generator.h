#ifndef IR_GENERATOR_H
#define IR_GENERATOR_H

#include "ir.h"
#include "parser.h"

typedef struct {
  char *name;
  type_def type;
  int initialized;

  value_id id;
} symbol;

typedef struct {
  char *name;
  type_def ret_type;
  type_def *arg_types;
  int argc;

  func_id id;
} function_def;

typedef struct {
  module *mod;
  function *current_func;
  block *current_block;

  symbol *symbols;
  int symbolc;
  function_def *functions;
  int defc;
} generator_state;

function *make_function(generator_state *state, char *name, type_def ret_type,
                        type_def *arg_types, char **arg_names, int argc);
block *make_block(generator_state *state);

symbol *find_value(generator_state *state, char *name);
function_def *find_function(generator_state *state, char *name);
symbol *push_symbol(generator_state *state, char *name, type_def type);

int make_type(generator_state *state, char *string, type_def *type);

int generate_ir(ast_node **source, int nodec, module *module);

#endif // !IR_GENERATOR_H
