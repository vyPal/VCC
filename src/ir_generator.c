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
    s.kind = SYM_VALUE;
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

int make_type(generator_state *state, parsed_type t, type_def *type) {
  if (strlen(t.base) == 0)
    return -1;
  if (t.pointer_depth > 0) {
    type_def *child = malloc(sizeof(type_def));
    if (child == NULL) {
      return -1;
    }
    t.pointer_depth--;
    if (make_type(state, t, child) < 0) {
      free(child);
      return -1;
    }
    type->kind = TY_PTR;
    type->base = child;
  } else if (strcmp(t.base, "char") == 0) {
    type->kind = TY_I8;
  } else if (strcmp(t.base, "short") == 0 || strcmp(t.base, "int") == 0 ||
             strcmp(t.base, "short int") == 0) {
    type->kind = TY_I16;
  } else if (strcmp(t.base, "long long") == 0) {
    type->kind = TY_I64;
  } else if (strcmp(t.base, "long") == 0 || strcmp(t.base, "long int") == 0) {
    type->kind = TY_I32;
  } else if (strcmp(t.base, "void") == 0) {
    type->kind = TY_VOID;
  } else {
    printf("Unknown type: %s\n", t.base);
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
        char *n = malloc(sizeof(char) * (name.len + 1));
        if (n == NULL) {
          printf("Failed to allocate space for arg name string\n");
          return -1;
        }
        memcpy(n, name.ptr, name.len);
        n[name.len] = 0;
        arg_names[i] = n;
        if (make_type(state, f->arg_types[i], arg_types + i) < 0) {
          return -1;
        }
      }
    }
    type_def ret_type;
    if (make_type(state, f->ret_type, &ret_type) < 0) {
      return -1;
    }
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
    if (make_type(state, v->type, type) < 0) {
      return -1;
    }
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
    new_symbol->kind = SYM_STACK;

    if (v->initializer != NULL) {
      state->requested_type = *type;
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

    state->requested_type = assign->type;
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
    i->ret = clone_type(rtyp);
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
  case UNARY_OP:;
    ast_node_unary_op *uop = (ast_node_unary_op *)node->node;

    if (strncmp("!", uop->op.ptr, uop->op.len) == 0) {
      if (generate_node(state, uop->val, 1, &val, &rtyp) < 0) {
        return -1;
      }
      i = new_instruction(state->current_func, state->current_block);
      if (i == NULL) {
        return -1;
      }
      i->value = val;
      i->op = IR_NOT;
      i->ret = clone_type(rtyp);
    } else if (strncmp("*", uop->op.ptr, uop->op.len) == 0) {
      if (generate_node(state, uop->val, 1, &val, &rtyp) < 0) {
        return -1;
      }
      i = new_instruction(state->current_func, state->current_block);
      if (i == NULL) {
        return -1;
      }
      i->value = val;
      i->op = IR_LOAD_ADDR;
      if (rtyp.kind != TY_PTR) {
        printf("Unable to dereference non-pointer type\n");
        return -1;
      }
      i->ret = clone_type(*rtyp.base);
    } else if (strncmp("&", uop->op.ptr, uop->op.len) == 0) {
      i = new_instruction(state->current_func, state->current_block);
      if (i == NULL) {
        return -1;
      }
      i->op = IR_ADDR;

      if (uop->val->type != LEAF) {
        printf("Unable to take reference to non-leaf\n");
        return -1;
      }
      ast_node_leaf *l = (ast_node_leaf *)uop->val->node;
      char *leaf = malloc(sizeof(char) * (l->len + 1));
      if (leaf == NULL) {
        return -1;
      }
      memcpy(leaf, l->ptr, l->len);
      leaf[l->len] = 0;

      symbol *lsym = find_value(state, leaf);
      if (lsym == NULL) {
        printf("Unable to find variable\n");
        return -1;
      } else {
        if (lsym->kind == SYM_STACK) {
          i->value = lsym->id;
        } else {
          printf("Unable to take reference to value not on stack\n");
          return -1;
        }
        rtyp = lsym->type;
      }
      free(leaf);

      type_def *type = malloc(sizeof(type_def));
      if (type == NULL) {
        printf("Unable to allocate memory for subtype\n");
        return -1;
      }
      *type = clone_type(rtyp);
      i->ret = pointer_to(type);
    } else {
      printf("Unknown unary operator\n");
      return -1;
    }

    *typ = i->ret;
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
      state->requested_type = state->current_func->ret_type;
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
    if (lsym == NULL) {
      i = new_instruction(state->current_func, state->current_block);
      if (i == NULL) {
        return -1;
      }
      char *end;
      long constant = strtol(leaf, &end, 10);
      i->op = IR_CONST;
      i->ret = clone_type(state->requested_type);
      i->constant = constant;
      *typ = i->ret;
      *ret = i->dst;
    } else {
      if (lsym->initialized == 0) {
        printf("Trying to load uninitialized value\n");
        return -1;
      }
      if (lsym->kind == SYM_STACK) {
        i = new_instruction(state->current_func, state->current_block);
        if (i == NULL) {
          return -1;
        }
        i->op = IR_LOAD;
        i->ret = lsym->type;
        i->value = lsym->id;
        *ret = i->dst;
      } else {
        *ret = lsym->id;
      }
      *typ = lsym->type;
    }
    free(leaf);
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
    value_id *args = malloc(sizeof(value_id) * c->argc);
    if (args == NULL) {
      return -1;
    }
    for (int i = 0; i < c->argc; i++) {
      state->requested_type = def->arg_types[i];
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
    i->ret = clone_type(def->ret_type);
    *typ = i->ret;
    i->call.argc = def->argc;
    i->call.args = args;
    i->call.func = func;
    i->call.type = def->ret_type;
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
