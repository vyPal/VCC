#include "compiler.h"
#include "parser.h"

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "utils.h"

variable *find_variable(compiler_state *state, token_slice func_name,
                        token_slice name) {
  for (int i = 0; i < state->varmapc; i++) {
    variable_map varmap = state->varmap[i];
    if (varmap.func_name.len == func_name.len &&
        strncmp(varmap.func_name.ptr, func_name.ptr, func_name.len) == 0) {
      for (int j = 0; j < varmap.variable_count; j++) {
        variable var = varmap.variables[j];
        if (var.name.len == name.len &&
            strncmp(var.name.ptr, name.ptr, name.len) == 0)
          return &varmap.variables[j];
      }
    }
  }
  return NULL;
}

int create_variable(compiler_state *state, token_slice func_name,
                    token_slice name) {
  variable_map *vmap = NULL;
  for (int i = 0; i < state->varmapc; i++) {
    variable_map varmap = state->varmap[i];
    if (varmap.func_name.len == func_name.len &&
        strncmp(varmap.func_name.ptr, func_name.ptr, func_name.len) == 0) {
      vmap = state->varmap + i;
    }
  }

  if (vmap == NULL) {
    variable_map *maps =
        realloc(state->varmap, sizeof(variable_map) * (state->varmapc + 1));
    if (maps == NULL) {
      printf("Failed to allocate space for new variable_map\n");
      for (int i = 0; i < state->varmapc; i++) {
        free(state->varmap[i].variables);
      }
      free(state->varmap);
      return -1;
    }
    variable_map new_map = {0};
    new_map.func_name = func_name;
    maps[state->varmapc] = new_map;
    vmap = maps + state->varmapc;
    state->varmap = maps;
    state->varmapc++;
  }

  if (vmap->variable_count == 0) {
    vmap->variables = malloc(sizeof(variable));
    if (vmap->variables == NULL) {
      printf("Failed to allocate space for variables\n");
      return -1;
    }
  } else {
    variable *new =
        realloc(vmap->variables, sizeof(variable) * (vmap->variable_count + 1));
    if (new == NULL) {
      free(vmap->variables);
      printf("Failed to reallocate space for variables\n");
      return -1;
    }
    vmap->variables = new;
  }
  vmap->variables[vmap->variable_count].name = name;
  vmap->current_stack_offset +=
      8; // TODO: Hardcoded to 8bytes (64bit) - make dynamic by type
  vmap->variables[vmap->variable_count].offset = vmap->current_stack_offset;
  vmap->variable_count++;
  return vmap->current_stack_offset;
}

// Appends null-terminated string to generated assembly
int append(compiler_state *state, char *text) {
  int len = strlen(text);
  if (state->output_len == 0) {
    state->generated = malloc(sizeof(char) * len);
    if (state->generated == NULL) {
      printf("Failed to allocate space for generated assembly\n");
      return -1;
    }
  } else {
    char *new =
        realloc(state->generated, sizeof(char) * (state->output_len + len));
    if (new == NULL) {
      free(state->generated);
      printf("Failed to reallocate space for generated assembly\n");
      return -1;
    }
    state->generated = new;
  }
  memcpy(state->generated + state->output_len, text, len);
  state->output_len += len;
  return len;
}

static inline char *arg_index_to_reg(int arg_idx) {
  switch (arg_idx) {
  case 0:
    return "rdi";
  case 1:
    return "rsi";
  case 2:
    return "rdx";
  case 3:
    return "rcx";
  case 4:
    return "r8";
  case 5:
    return "r9";
  default:
    return NULL;
  }
}

char *to_pretty(ast_node *node) {
  char *ret;
  int err;
  string_builder builder;
  switch (node->type) {
  case FUNCTION:;
    ast_node_function *f = (ast_node_function *)node->node;
    err = sb_init(&builder);
    if (err == -1)
      return NULL;
    err = asprintf(&ret, "%.*s %.*s(", f->ret_type.len, f->ret_type.ptr,
                   f->name.len, f->name.ptr);
    if (err == -1) {
      sb_free(&builder);
      free(ret);
      return NULL;
    }
    err = sb_append_free(&builder, ret);
    if (err == -1) {
      sb_free(&builder);
      return NULL;
    }
    for (int i = 0; i < f->argc; i++) {
      token_slice ty = f->arg_types[i];
      token_slice name = f->arg_names[i];
      err = asprintf(&ret, "%.*s %.*s", ty.len, ty.ptr, name.len, name.ptr);
      if (err == -1) {
        sb_free(&builder);
        free(ret);
        return NULL;
      }
      err = sb_append_free(&builder, ret);
      if (err == -1) {
        sb_free(&builder);
        return NULL;
      }
      if (i != f->argc - 1) {
        err = sb_append(&builder, ", ");
        if (err == -1) {
          sb_free(&builder);
          return NULL;
        }
      }
    }
    err = sb_append(&builder, ")");
    if (err == -1) {
      sb_free(&builder);
      return NULL;
    }
    ret = sb_build(&builder);
    break;
  case VARIABLE:;
    ast_node_variable *v = (ast_node_variable *)node->node;
    if (v->initializer == NULL)
      err = asprintf(&ret, "%.*s %.*s;", v->type.len, v->type.ptr, v->name.len,
                     v->name.ptr);
    else {
      char *init = to_pretty(v->initializer);
      if (init == NULL)
        return NULL;

      err = asprintf(&ret, "%.*s %.*s = %s;", v->type.len, v->type.ptr,
                     v->name.len, v->name.ptr, init);
      free(init);
    }
    break;
  case ASSIGNMENT:;
    ast_node_assignment *a = (ast_node_assignment *)node->node;
    char *value = to_pretty(a->value);
    if (value == NULL)
      return NULL;
    err = asprintf(&ret, "%.*s = %s;", a->name.len, a->name.ptr, value);
    free(value);
    break;
  case BINARY_OP:;
    ast_node_binary_op *b = (ast_node_binary_op *)node->node;
    char *left = to_pretty(b->left);
    if (left == NULL)
      return NULL;
    char *right = to_pretty(b->right);
    if (right == NULL)
      return NULL;
    err = asprintf(&ret, "%s %.*s %s", left, b->op.len, b->op.ptr, right);
    free(left);
    free(right);
    break;
  case RETURN:;
    if (node->node == NULL) {
      err = asprintf(&ret, "return void;");
    } else {
      char *ret_value = to_pretty(node->node);
      if (ret_value == NULL)
        return NULL;
      err = asprintf(&ret, "return %s;", ret_value);
      free(ret_value);
    }
    break;
  case LEAF:;
    token_slice s = *((token_slice *)node->node);
    err = asprintf(&ret, "%.*s", s.len, s.ptr);
    break;
  case CALL:;
    ast_node_call *c = (ast_node_call *)node->node;
    err = sb_init(&builder);
    if (err == -1)
      return NULL;
    err = asprintf(&ret, "%.*s(", c->name.len, c->name.ptr);
    if (err == -1) {
      sb_free(&builder);
      free(ret);
      return NULL;
    }
    err = sb_append_free(&builder, ret);
    if (err == -1) {
      sb_free(&builder);
      return NULL;
    }
    for (int i = 0; i < c->argc; i++) {
      char *arg = to_pretty(c->args[i]);
      if (arg == NULL) {
        sb_free(&builder);
        return NULL;
      }
      err = sb_append_free(&builder, arg);
      if (err == -1) {
        sb_free(&builder);
        return NULL;
      }
      if (i != c->argc - 1) {
        err = sb_append(&builder, ", ");
        if (err == -1) {
          sb_free(&builder);
          return NULL;
        }
      }
    }
    err = sb_append(&builder, ")");
    if (err == -1) {
      sb_free(&builder);
      return NULL;
    }
    ret = sb_build(&builder);
  }
  if (err == -1)
    return NULL;
  return ret;
}

int emit_prologue(compiler_state *state) {
  return append(state, "; Program prologue\nsection .text\nglobal "
                       "_start\n_start:\n\tcall main\n\n\tmov rdi, rax ; exit "
                       "code\n\tmov rax, 60 ; sys_exit\n\tsyscall\n\n");
}

int emit_leaf(compiler_state *state, ast_node_leaf leaf, token_slice parent) {
  variable *var = find_variable(state, parent, leaf);
  char *buf;
  int ret;
  if (var == NULL) { // FIXME: Very bad way to handle this xD
    ret = asprintf(&buf, "\tmov rax, %.*s\n", leaf.len, leaf.ptr);
    if (ret == -1)
      return -1;
  } else {
    ast_node tmp_node;
    tmp_node.type = LEAF;
    tmp_node.node = &leaf;
    char *comment = to_pretty(&tmp_node);
    if (comment == NULL) {
      printf("Failed to generate comment\n");
      return -1;
    }
    ret = asprintf(&buf, "\tmov rax, QWORD [rbp-%d]\t\t; %s\n", var->offset,
                   comment);
    if (ret == -1)
      return -1;
    free(comment);
  }
  ret = append(state, buf);
  free(buf);
  return ret;
}

int emit_return(compiler_state *state, ast_node_return ret_node) {
  ast_node tmp_node;
  tmp_node.type = RETURN;
  tmp_node.node = ret_node;
  char *comment = to_pretty(&tmp_node);
  if (comment == NULL) {
    printf("Failed to generate comment\n");
    return -1;
  }
  int ret = append(state, "\n; ");
  if (ret < 0) {
    free(comment);
    return ret;
  }
  ret = append(state, comment);
  free(comment);
  if (ret < 0)
    return ret;
  if (ret_node == NULL) {
    int ret = append(state, "\nnop\n");
    if (ret < 0)
      return ret;
  }
  return append(state, "\n\tmov rsp, rbp\n\tpop rbp\n\tret\n\n");
}

int emit_function_call(compiler_state *state, ast_node_call c,
                       token_slice *parent) {
  int ret;
  for (int i = c.argc - 1; i >= 0; i--) {
    ret = generate_node(state, c.args[i], parent);
    if (ret < 0)
      return ret;
    char *reg = arg_index_to_reg(i);
    if (reg == NULL) {
      printf("Can not support calling function with more than 6 arguments\n");
      return -1;
    }
    ret = append(state, "\tmov ");
    if (ret < 0)
      return ret;
    ret = append(state, reg);
    if (ret < 0)
      return ret;
    ret = append(state, ", rax\n");
    if (ret < 0)
      return ret;
  }

  ast_node tmp_node;
  tmp_node.type = CALL;
  tmp_node.node = &c;
  char *comment = to_pretty(&tmp_node);
  char *buf;
  ret = asprintf(&buf, "\tcall %.*s\t\t\t; %s\n", c.name.len, c.name.ptr,
                 comment);
  free(comment);
  if (ret == -1)
    return -1;
  ret = append(state, buf);
  free(buf);
  return ret;
}

int emit_function_prologue(compiler_state *state, ast_node_function *func) {
  char *buf;
  ast_node tmp_node;
  tmp_node.type = FUNCTION;
  tmp_node.node = func;
  char *comment = to_pretty(&tmp_node);
  if (comment == NULL) {
    printf("Failed to generate comment\n");
    return -1;
  }
  int align = (func->localc + func->argc % 2) != 0;
  int ret = asprintf(
      &buf,
      "; Function prologue\n%.*s:\t\t\t\t\t; %s\n\tpush rbp\n\tmov rbp, "
      "rsp\n\tsub rsp, %d\n\n",
      func->name.len, func->name.ptr, comment,
      (func->localc + func->argc + align) * 8);
  free(comment);
  if (ret == -1)
    return -1;
  ret = append(state, buf);
  free(buf);

  for (int i = 0; i < func->argc; i++) {
    token_slice name = func->arg_names[i];
    if (find_variable(state, func->name, name) != NULL) {
      printf("Redefinition of variable\n");
      return -1;
    }
    int offset = create_variable(state, func->name, name);
    if (offset < 0) {
      printf("Invalid offset for variable\n");
      return offset;
    }

    char *reg = arg_index_to_reg(i);
    if (reg == NULL) {
      printf("Can not support calling function with more than 6 arguments\n");
      return -1;
    }

    ret = asprintf(&buf, "\tmov QWORD [rbp-%d], %s\t\t; %.*s\n\n", offset, reg,
                   name.len, name.ptr);
    if (ret == -1)
      return ret;
    ret = append(state, buf);
    free(buf);
  }

  return ret;
}

int generate_node(compiler_state *state, ast_node *node, token_slice *parent) {
  int ret;
  char *comment;
  char *buf;
  switch (node->type) {
  case FUNCTION:;
    ast_node_function *f = (ast_node_function *)node->node;
    ret = emit_function_prologue(state, f);
    if (ret < 0) {
      printf("Failed to emit function prologue\n");
      return ret;
    }

    for (int i = 0; i < f->nodec; i++) {
      ret = generate_node(state, f->nodes[i], &f->name);
      if (ret < 0) {
        printf("Failed to generate function body\n");
        return ret;
      }
    }
    break;
  case VARIABLE:;
    ast_node_variable *v = (ast_node_variable *)node->node;
    if (find_variable(state, *parent, v->name) != NULL) {
      printf("Redefinition of variable\n");
      return -1;
    }
    int offset = create_variable(state, *parent, v->name);
    if (offset < 0) {
      printf("Invalid offset for variable\n");
      return offset;
    }

    comment = to_pretty(node);
    if (comment == NULL)
      return -1;
    if (v->initializer != NULL) {
      ret = generate_node(state, v->initializer, parent);
      if (ret < 0) {
        printf("Failed to generate variable initializer\n");
        return ret;
      }
      ret = asprintf(&buf, "\tmov QWORD [rbp-%d], rax\t\t; %s\n\n", offset,
                     comment);
      free(comment);
      if (ret == -1)
        return ret;
      ret = append(state, buf);
      free(buf);
      if (ret < 0)
        return ret;
    } else {
      ret = asprintf(&buf, "\t\t\t\t; %s\n", comment);
      free(comment);
      if (ret == -1)
        return ret;
      ret = append(state, buf);
      free(buf);
      if (ret < 0)
        return ret;
    }
    break;
  case ASSIGNMENT:;
    ast_node_assignment *a = (ast_node_assignment *)node->node;
    variable *var = find_variable(state, *parent, a->name);
    if (var == NULL) {
      printf("Undefined variable\n");
      return -1;
    }

    ret = generate_node(state, a->value, parent);
    comment = to_pretty(node);
    if (comment == NULL)
      return -1;
    ret = asprintf(&buf, "\tmov QWORD [rbp-%d], rax\t\t; %s\n\n", var->offset,
                   comment);
    free(comment);
    if (ret == -1)
      return -1;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    break;
  case BINARY_OP:;
    ast_node_binary_op *b = (ast_node_binary_op *)node->node;
    comment = to_pretty(node);
    if (comment == NULL)
      return -1;
    ret = generate_node(state, b->right, parent);
    if (ret < 0)
      return ret;
    ret = append(state, "\tmov rdi, rax\n");
    if (ret < 0)
      return ret;
    ret = generate_node(state, b->left, parent);
    if (ret < 0)
      return ret;

    if (strncmp("+", b->op.ptr, b->op.len) == 0) {
      ret = append(state, "\tadd rax, rdi\t\t\t; ");
      if (ret < 0)
        return ret;
    } else if (strncmp("-", b->op.ptr, b->op.len) == 0) {
      ret = append(state, "\tsub rax, rdi\t\t\t; ");
      if (ret < 0)
        return ret;
    } else if (strncmp("*", b->op.ptr, b->op.len) == 0) {
      ret = append(state, "\tmul rax, rdi\t\t\t; ");
      if (ret < 0)
        return ret;
    } else if (strncmp("/", b->op.ptr, b->op.len) == 0) {
      ret = append(state, "\tcqo\n\tidiv rdi\t\t\t; ");
      if (ret < 0)
        return ret;
    } else if (strncmp("%", b->op.ptr, b->op.len) == 0) {
      ret = append(state, "\tcqo\n\tidiv rdi\n\tmov rax, rdx\t\t\t; ");
      if (ret < 0)
        return ret;
    } else {
      printf("Unknown operator\n");
      return -1;
    }
    ret = append(state, comment);
    free(comment);
    if (ret < 0)
      return ret;
    ret = append(state, "\n\n");
    if (ret < 0)
      return ret;
    break;
  case RETURN:;
    if (node->node != NULL) {
      ret = generate_node(state, node->node, parent);
      if (ret < 0) {
        printf("Failed to generate return value\n");
        return ret;
      }
    }
    ret = emit_return(state, (ast_node_return)node->node);
    if (ret < 0) {
      printf("Failed to emit function epilogue\n");
      return ret;
    }
    break;
  case LEAF:
    ret = emit_leaf(state, *((ast_node_leaf *)node->node), *parent);
    if (ret < 0)
      return ret;
    break;
  case CALL:
    ret = emit_function_call(state, *((ast_node_call *)node->node), parent);
    if (ret < 0)
      return ret;
  }
  return 0;
}

int generate_asm(ast_node **source, int nodec, char **output) {
  compiler_state state = {0};
  int ret = emit_prologue(&state);
  if (ret < 0) {
    printf("Failed to emit program epilogue\n");
    free(state.generated);
    return ret;
  }

  for (int i = 0; i < nodec; i++) {
    ret = generate_node(&state, source[i], NULL);
    if (ret < 0) {
      free(state.generated);
      for (int i = 0; i < state.varmapc; i++) {
        free(state.varmap[i].variables);
      }
      free(state.varmap);
      return ret;
    }
  }

  *output = state.generated;
  for (int i = 0; i < state.varmapc; i++) {
    free(state.varmap[i].variables);
  }
  free(state.varmap);
  return state.output_len;
}
