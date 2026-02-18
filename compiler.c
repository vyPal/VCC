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

int emit_prologue(compiler_state *state) {
  return append(state, "; Program prologue\nsection .text\nglobal "
                       "_start\n_start:\n\tcall main\n\n\tmov rdi, rax ; exit "
                       "code\n\tmov rax, 60 ; sys_exit\n\tsyscall\n\n");
}

int emit_leaf(compiler_state *state, ast_node_leaf leaf) {
  variable *var = find_variable(state, leaf);
  if (var == NULL) { // FIXME: Very bad way to handle this xD
    char buf[128];   // TODO: Should be dynamic
    sprintf(buf, "\tmov rax, %.*s\n", leaf.len, leaf.ptr);
    return append(state, buf);
  } else {
    char buf[128]; // TODO: Should be dynamic
    sprintf(buf, "\tmov rax, QWORD [rbp-%d]\n", var->offset);
    return append(state, buf);
  }
}

int emit_return(compiler_state *state, ast_node_return ret_node) {
  if (ret_node == NULL) {
    int ret = append(state, "nop ; Return void\n");
    if (ret < 0) {
      return ret;
    }
  }
  return append(state,
                "; Function epilogue\n\tmov rsp, rbp\n\tpop rbp\n\tret\n");
}

int emit_function_prologue(compiler_state *state, ast_node_function *func) {
  char buf[128]; // TODO: Should be dynamic
  sprintf(buf, "; Function prologue\n%.*s:\n\tpush rbp\n\tmov rbp, rsp\n",
          func->name.len, func->name.ptr);
  return append(state, buf);
}

int generate_node(compiler_state *state, ast_node *node) {
  int ret;
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

    if (v->initializer != NULL) {
      ret = generate_node(state, v->initializer);
      if (ret < 0) {
        printf("Failed to generate variable initializer\n");
        return ret;
      }
      char buf[128];
      sprintf(buf, "\tmov QWORD [rbp-%d], rax\n", offset);
      ret = append(state, buf);
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
    char buf[128];
    sprintf(buf, "\tmov QWORD [rbp-%d], rax\n", var->offset);
    ret = append(state, buf);
    if (ret < 0)
      return ret;
    break;
  case BINARY_OP:;
    ast_node_binary_op *b = (ast_node_binary_op *)node->node;
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
      ret = append(state, "\tadd rax, rdi\n");
      if (ret < 0)
        return ret;
    } else if (strncmp("-", b->op.ptr, b->op.len) == 0) {
      ret = append(state, "\tsub rax, rdi\n");
      if (ret < 0)
        return ret;
    } else {
      printf("Unknown operator\n");
      return -1;
    }
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
