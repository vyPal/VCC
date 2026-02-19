#include "compiler.h"
#include "parser.h"

#include "stdio.h"
#include "stdlib.h"
#include "string.h"

typedef struct {
  token_slice name;
  int offset;
} variable;

typedef struct {
  char *generated;
  int output_len;
  variable *variables;
  int variable_count;
  int current_stack_offset;
} compiler_state;

variable *find_variable(compiler_state *state, token_slice name) {
  for (int i = 0; i < state->variable_count; i++) {
    variable var = state->variables[i];
    if (var.name.len == name.len &&
        strncmp(var.name.ptr, name.ptr, name.len) == 0)
      return &state->variables[i];
  }
  return NULL;
}

int create_variable(compiler_state *state, token_slice name) {
  if (state->variable_count == 0) {
    state->variables = malloc(sizeof(variable));
    if (state->variables == NULL) {
      printf("Failed to allocate space for variables\n");
      return -1;
    }
  } else {
    variable *new = realloc(state->variables,
                            sizeof(variable) * (state->variable_count + 1));
    if (new == NULL) {
      free(state->variables);
      printf("Failed to reallocate space for variables\n");
      return -1;
    }
    state->variables = new;
  }
  state->variables[state->variable_count].name = name;
  state->variables[state->variable_count].offset = state->current_stack_offset;
  state->current_stack_offset +=
      8; // TODO: Hardcoded to 8bytes (64bit) - make dynamic by type
  state->variable_count++;
  return state->current_stack_offset - 8;
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

char *to_pretty(ast_node *node) {
  char *ret;
  int err;
  switch (node->type) {
  case FUNCTION:;
    ast_node_function *f = (ast_node_function *)node->node;
    err = asprintf(&ret, "%.*s %.*s();", f->ret_type.len, f->ret_type.ptr,
                   f->name.len, f->name.ptr);
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

int emit_leaf(compiler_state *state, ast_node_leaf leaf) {
  variable *var = find_variable(state, leaf);
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
  int ret = append(state, "\t\t\t\t\t; ");
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
  return append(state, "\n\tmov rsp, rbp\n\tpop rbp\n\tret\n");
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
  int ret = asprintf(
      &buf,
      "; Function prologue\n%.*s:\t\t\t\t\t; %s\n\tpush rbp\n\tmov rbp, "
      "rsp\n\n",
      func->name.len, func->name.ptr, comment);
  free(comment);
  if (ret == -1)
    return -1;
  ret = append(state, buf);
  free(buf);
  return ret;
}

int generate_node(compiler_state *state, ast_node *node) {
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
      ret = generate_node(state, f->nodes[i]);
      if (ret < 0) {
        printf("Failed to generate function body\n");
        return ret;
      }
    }
    break;
  case VARIABLE:;
    ast_node_variable *v = (ast_node_variable *)node->node;
    if (find_variable(state, v->name) != NULL) {
      printf("Redefinition of variable\n");
      return -1;
    }
    int offset = create_variable(state, v->name);
    if (offset < 0) {
      printf("Invalid offset for variable\n");
      return offset;
    }

    comment = to_pretty(node);
    if (comment == NULL)
      return -1;
    if (v->initializer != NULL) {
      ret = generate_node(state, v->initializer);
      if (ret < 0) {
        printf("Failed to generate variable initializer\n");
        return ret;
      }
      ret = asprintf(&buf, "\tmov QWORD [rbp-%d], rax\t\t; %s\n", offset,
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
    variable *var = find_variable(state, a->name);
    if (var == NULL) {
      printf("Undefined variable\n");
      return -1;
    }

    ret = generate_node(state, a->value);
    comment = to_pretty(node);
    if (comment == NULL)
      return -1;
    ret = asprintf(&buf, "\tmov QWORD [rbp-%d], rax\t\t; %s\n", var->offset,
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
    ret = generate_node(state, b->right);
    if (ret < 0)
      return ret;
    ret = append(state, "\tmov rdi, rax\n");
    if (ret < 0)
      return ret;
    ret = generate_node(state, b->left);
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
    ret = append(state, "\n");
    if (ret < 0)
      return ret;
    break;
  case RETURN:;
    if (node->node != NULL) {
      ret = generate_node(state, node->node);
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
    ret = emit_leaf(state, *((ast_node_leaf *)node->node));
    if (ret < 0)
      return ret;
    break;
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
    ret = generate_node(&state, source[i]);
    if (ret < 0) {
      free(state.generated);
      free(state.variables);
      return ret;
    }
  }

  *output = state.generated;
  free(state.variables);
  return state.output_len;
}
