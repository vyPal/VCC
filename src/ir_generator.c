#include "ir_generator.h"
#include "ir.h"
#include "parser.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

// TODO: Clean state properly

function *make_function(generator_state *state, char *name, type_def ret_type,
                        type_def *arg_types, char **arg_names, int argc) {
  if (state == NULL)
    return NULL;

  function *new = new_function(state->mod);
  if (new == NULL)
    return NULL;

  function_def def = {0};
  new->name = def.name = name;
  new->ret_type = def.ret_type = ret_type;

  if (state->symbols != NULL)
    free(state->symbols);
  state->symbolc = argc;
  state->symbols = malloc(sizeof(symbol) * argc);
  if (state->symbols == NULL) {
    // TODO: Handle correctly
    return NULL;
  }

  int ret;
  for (int i = 0; i < argc; i++) {
    value_id id = new_value(new);
    ret = function_add_argument(new, arg_types[i], id);
    if (ret < 0) {
      // TODO: Handle correctly
      return NULL;
    }
    symbol s = {0};
    s.id = id;
    s.name = arg_names[i];
    s.type = arg_types[i];
    s.initialized = 1;
    state->symbols[i] = s;
  }
  def.arg_types = new->arg_types;
  def.argc = new->argc;

  def.id = new->id;

  function_def *new_defs =
      realloc(state->functions, sizeof(function_def) * (state->defc + 1));
  if (new_defs == NULL) {
    printf("Failed to allocate space for function definition array\n");
    if (state->functions != NULL)
      free(state->functions);
    if (state->symbols != NULL)
      free(state->symbols);
    clean_module(state->mod);
    return NULL;
  }
  state->functions = new_defs;
  state->functions[state->defc++] = def;

  state->current_func = new;

  return new;
}

block *make_block(generator_state *state) {
  if (state == NULL)
    return NULL;
  if (state->current_func == NULL) {
    printf("Not currently inside a function\n");
    clean_module(state->mod);
    return NULL;
  }

  block *new = new_block(state->current_func);
  if (new == NULL)
    return NULL;

  state->current_block = new;

  return new;
}

symbol *find_value(generator_state *state, char *name) {
  if (state == NULL)
    return NULL;
  if (state->current_func == NULL) {
    printf("Not currently inside a function\n");
    clean_module(state->mod);
    return NULL;
  }
  if (state->symbols == NULL) {
    printf("Function doesn't have a symbol table\n");
    clean_module(state->mod);
    return NULL;
  }
  for (int i = 0; i < state->symbolc; i++) {
    if (strcmp(state->symbols[i].name, name) == 0) {
      return state->symbols + i;
    }
  }
  return NULL;
}

function_def *find_function(generator_state *state, char *name) {
  if (state == NULL)
    return NULL;
  if (state->functions == NULL) {
    printf("No function records defined\n");
    clean_module(state->mod);
    return NULL;
  }
  for (int i = 0; i < state->defc; i++) {
    if (strcmp(state->functions[i].name, name) == 0) {
      return state->functions + i;
    }
  }
  return NULL;
}

symbol *push_symbol(generator_state *state, char *name, type_def type) {
  if (state == NULL)
    return NULL;

  symbol *new = realloc(state->symbols, sizeof(symbol) * (state->symbolc + 1));
  if (new == NULL) {
    if (state->symbols != NULL)
      free(state->symbols);
    return NULL;
  }
  state->symbols = new;

  symbol s = {0};
  s.name = name;
  s.type = type;
  state->symbols[state->symbolc] = s;

  return state->symbols + (state->symbolc++);
}

int make_type(generator_state *state, char *string, type_def *type) {
  if (strlen(string) == 0)
    return -1;
  if (string[0] == '*') {
    type_def *child = malloc(sizeof(type_def));
    if (child == NULL) {
      return -1;
    }
    if (make_type(state, ++string, child) < 0) {
      free(child);
      return -1;
    }
    type->kind = TY_PTR;
    type->base = child;
  } else if (strcmp(string, "int") ==
             0) { // TODO: Handle types properlly once full support
    type->kind = TY_I32;
  } else if (strcmp(string, "long") == 0) {
    type->kind = TY_I64;
  } else if (strcmp(string, "void") == 0) {
    type->kind = TY_VOID;
  } else {
    printf("Unknown type: %s\n", string);
    return -1;
  }
  return 0;
}

int generate_node(generator_state *state, ast_node *node, int in_function,
                  value_id *ret, type_def *typ) {
  if (in_function && node->type == FUNCTION) {
    printf("Function inside function is not allowed\n");
    return -1;
  }

  value_id val = 0;
  type_def rtyp = {0};
  instruction *i;

  // TODO: Handle freeing memory is error cases
  switch (node->type) {
  case FUNCTION:;
    ast_node_function *f = (ast_node_function *)node->node;
    type_def *arg_types = NULL;
    char **arg_names = NULL;
    if (f->argc > 0) {
      arg_types = malloc(sizeof(type_def) * f->argc);
      if (arg_types == NULL) {
        printf("Failed to allocate space for argument list\n");
        return -1;
      }
      arg_names = malloc(sizeof(char *) * f->argc);
      if (arg_names == NULL) {
        printf("Failed to allocate space for argument list\n");
        return -1;
      }
      for (int i = 0; i < f->argc; i++) {
        token_slice name = f->arg_names[i];
        token_slice type = f->arg_types[i];
        char *ty = malloc(sizeof(char) * (type.len + 1));
        if (ty == NULL) {
          printf("Failed to allocate space for type string\n");
          return -1;
        }
        memcpy(ty, type.ptr, type.len);
        ty[type.len] = 0;
        char *n = malloc(sizeof(char) * (name.len + 1));
        if (n == NULL) {
          printf("Failed to allocate space for arg name string\n");
          return -1;
        }
        memcpy(n, name.ptr, name.len);
        n[name.len] = 0;
        arg_names[i] = n;
        if (make_type(state, ty, arg_types + i) < 0) {
          return -1;
        }
        free(ty);
      }
    }
    type_def ret_type;
    char *ret_ty = malloc(sizeof(char) * (f->ret_type.len + 1));
    if (ret_ty == NULL) {
      printf("Failed to allocate space for type string\n");
      return -1;
    }
    memcpy(ret_ty, f->ret_type.ptr, f->ret_type.len);
    ret_ty[f->ret_type.len] = 0;
    if (make_type(state, ret_ty, &ret_type) < 0) {
      return -1;
    }
    free(ret_ty);
    char *name = malloc(sizeof(char) * (f->name.len + 1));
    if (name == NULL) {
      printf("Failed to allocate space for name string\n");
      return -1;
    }
    memcpy(name, f->name.ptr, f->name.len);
    name[f->name.len] = 0;

    function *fn =
        make_function(state, name, ret_type, arg_types, arg_names, f->argc);
    if (fn == NULL) {
      return -1;
    }

    block *b = make_block(state);
    if (b == NULL) {
      return -1;
    }
    /* TODO: Use this for branching later
  b->label = malloc(sizeof(char) * 6);
  if (b->label == NULL) {
    return -1;
  }
  strcpy(b->label, "entry");
    */

    for (int i = 0; i < f->nodec; i++) {
      if (generate_node(state, f->nodes[i], 1, &val, &rtyp) < 0) {
        return -1;
      }
    }

    break;
  case VARIABLE:;
    ast_node_variable *v = (ast_node_variable *)node->node;

    type_def *type = malloc(sizeof(type_def));
    if (type == NULL) {
      return -1;
    }
    char *ty = malloc(sizeof(char) * (v->type.len + 1));
    if (ty == NULL) {
      printf("Failed to allocate space for type string\n");
      return -1;
    }
    memcpy(ty, v->type.ptr, v->type.len);
    ty[v->type.len] = 0;
    if (make_type(state, ty, type) < 0) {
      return -1;
    }
    free(ty);
    char *varname = malloc(sizeof(char) * (v->name.len + 1));
    if (varname == NULL) {
      printf("Failed to allocate space for name string\n");
      return -1;
    }
    memcpy(varname, v->name.ptr, v->name.len);
    varname[v->name.len] = 0;

    symbol *new_symbol = push_symbol(state, varname, *type);
    if (new_symbol == NULL) {
      return -1;
    }

    i = new_instruction(state->current_func, state->current_block);
    if (i == NULL) {
      return -1;
    }
    i->op = IR_ALLOCA;
    i->ret = pointer_to(type);
    *typ = i->ret;
    i->alloca.type = *type;
    i->alloca.count = 1; // TODO: Make dynamic once arrays are implemented

    new_symbol->id = i->dst;

    if (v->initializer != NULL) {
      if (generate_node(state, v->initializer, 1, &val, &rtyp) < 0) {
        return -1;
      }
      i = new_instruction(state->current_func, state->current_block);
      if (i == NULL) {
        return -1;
      }
      i->op = IR_STORE;
      i->ret.kind =
          TY_VOID; // TODO: Need to implement type checking for the initializer
      i->binop.lhs = new_symbol->id;
      i->binop.rhs = val;
      new_symbol->initialized = 1;
    }
    *ret = new_symbol->id;
    break;
  case ASSIGNMENT:;
    ast_node_assignment *a = (ast_node_assignment *)node->node;
    char *assign_name = malloc(sizeof(char) * (a->name.len + 1));
    if (assign_name == NULL) {
      return -1;
    }
    memcpy(assign_name, a->name.ptr, a->name.len);
    assign_name[a->name.len] = 0;
    symbol *assign = find_value(state, assign_name);
    free(assign_name);
    if (assign == NULL) {
      printf("Couldn't find variable\n");
      return -1;
    }
    assign->initialized = 1;

    if (generate_node(state, a->value, 1, &val, &rtyp) < 0) {
      return -1;
    }
    i = new_instruction(state->current_func, state->current_block);
    if (i == NULL) {
      return -1;
    }
    i->op = IR_STORE;
    i->ret.kind =
        TY_VOID; // TODO: Need to implement type checking for the value
    *typ = i->ret;
    i->binop.lhs = assign->id;
    i->binop.rhs = val;
    *ret = i->dst;
    break;
  case BINARY_OP:;
    ast_node_binary_op *op = (ast_node_binary_op *)node->node;
    if (generate_node(state, op->left, 1, &val, &rtyp) < 0) {
      return -1;
    }
    value_id rhs;
    if (generate_node(state, op->right, 1, &rhs, &rtyp) < 0) {
      return -1;
    }
    i = new_instruction(state->current_func, state->current_block);
    if (i == NULL) {
      return -1;
    }
    i->ret = rtyp;
    *typ = i->ret;
    i->binop.lhs = val;
    i->binop.rhs = rhs;

    if (strncmp("+", op->op.ptr, op->op.len) == 0) {
      i->op = IR_ADD;
    } else if (strncmp("-", op->op.ptr, op->op.len) == 0) {
      i->op = IR_SUB;
    } else if (strncmp("*", op->op.ptr, op->op.len) == 0) {
      i->op = IR_MUL;
    } else if (strncmp("/", op->op.ptr, op->op.len) == 0) {
      i->op = IR_SDIV;
    } else if (strncmp("%", op->op.ptr, op->op.len) == 0) {
      i->op = IR_SREM;
    } else {
      printf("Unknown operator\n");
      return -1;
    }
    *ret = i->dst;
    break;
  case RETURN:;
    if (node->node == NULL) {
      i = new_instruction(state->current_func, state->current_block);
      if (i == NULL) {
        return -1;
      }
      i->op = IR_RET;
      i->optional.present = 0;
    } else {
      if (generate_node(state, node->node, 1, &val, &rtyp) < 0) {
        return -1;
      }
      i = new_instruction(state->current_func, state->current_block);
      if (i == NULL) {
        return -1;
      }
      i->op = IR_RET;
      i->optional.present = 1;
      i->optional.value = val;
    }
    i->ret.kind = TY_VOID;
    *typ = i->ret;
    *ret = i->dst;
    break;
  case LEAF:;
    ast_node_leaf *l = (ast_node_leaf *)node->node;
    char *leaf = malloc(sizeof(char) * (l->len + 1));
    if (leaf == NULL) {
      return -1;
    }
    memcpy(leaf, l->ptr, l->len);
    leaf[l->len] = 0;

    symbol *lsym = find_value(state, leaf);
    i = new_instruction(state->current_func, state->current_block);
    if (i == NULL) {
      return -1;
    }
    if (lsym == NULL) {
      char *end;
      long constant = strtol(leaf, &end, 10);
      i->op = IR_CONST;
      i->ret.kind = TY_I32; // TODO: Determine
      i->constant = constant;
    } else {
      if (lsym->initialized == 0) {
        printf("Trying to load uninitialized value\n");
        return -1;
      }
      i->op = IR_LOAD;
      i->ret = lsym->type;
      i->value = lsym->id;
    }
    free(leaf);
    *typ = i->ret;
    *ret = i->dst;
    break;
  case CALL:;
    ast_node_call *c = (ast_node_call *)node->node;
    char *func = malloc(sizeof(char) * (c->name.len + 1));
    if (func == NULL) {
      return -1;
    }
    memcpy(func, c->name.ptr, c->name.len);
    func[c->name.len] = 0;

    function_def *def = find_function(state, func);
    if (def == NULL) {
      printf("Couldn't find function with name: %s\n", func);
      return -1;
    }
    free(func);
    value_id *args = malloc(sizeof(value_id) * c->argc);
    if (args == NULL) {
      return -1;
    }
    for (int i = 0; i < c->argc; i++) {
      if (generate_node(state, c->args[i], 1, &val, &rtyp) < 0) {
        return -1;
      }
      args[i] = val;
    }
    i = new_instruction(state->current_func, state->current_block);
    if (i == NULL) {
      return -1;
    }
    i->op = IR_CALL;
    i->ret = def->ret_type;
    *typ = i->ret;
    i->call.argc = def->argc;
    i->call.args = args;
    i->call.func = def->id;
    *ret = i->dst;
    break;
  }
  return 0;
}

void clean_state(generator_state *state) {
  free(state->functions);
  for (int i = 0; i < state->symbolc; i++) {
    free(state->symbols[i].name);
  }
  free(state->symbols);
}

int generate_ir(ast_node **source, int nodec, module *module) {
  generator_state state = {0};
  state.mod = module;
  int ret;
  value_id val;
  type_def rtyp;
  for (int i = 0; i < nodec; i++) {
    ret = generate_node(&state, source[i], 0, &val, &rtyp);
    if (ret < 0)
      return -1;
  }
  clean_state(&state);
  return 0;
}
