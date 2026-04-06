#include "ir.h"
#include "stdio.h"
#include "stdlib.h"

module new_module() {
  module m = {0};
  return m;
}

function *new_function(module *m) {
  if (m == NULL)
    return NULL;

  if (m->function_count + 1 > m->function_capacity) {
    m->function_capacity *= 2;
    if (m->function_capacity == 0)
      m->function_capacity = 1;
    function *new =
        realloc(m->functions, sizeof(function) * m->function_capacity);
    if (new == NULL) {
      printf("Failed to allocate memory for function list\n");
      if (m->functions != NULL)
        free(m->functions);
      return NULL;
    }
    m->functions = new;
  }

  function func = {0};
  func.id = m->function_count;
  m->functions[m->function_count] = func;

  return m->functions + (m->function_count++);
}

value_id new_value(function *f) { return f->next_value_id++; }

int function_add_argument(function *f, type_def t, value_id id) {
  if (f == NULL)
    return -1;

  f->argc++;
  type_def *types = realloc(f->arg_types, sizeof(type_def) * f->argc);
  if (types == NULL) {
    printf("Failed to allocate memory for argument list\n");
    if (f->arg_types != NULL)
      free(f->arg_types);
    if (f->arg_values != NULL)
      free(f->arg_values);
    return -1;
  }
  f->arg_types = types;

  value_id *values = realloc(f->arg_values, sizeof(value_id) * f->argc);
  if (values == NULL) {
    printf("Failed to allocate memory for argument list\n");
    if (f->arg_types != NULL)
      free(f->arg_types);
    if (f->arg_values != NULL)
      free(f->arg_values);
    return -1;
  }
  f->arg_values = values;

  f->arg_types[f->argc - 1] = t;
  f->arg_values[f->argc - 1] = id;
  return 0;
}

block *new_block(function *f) {
  if (f == NULL)
    return NULL;

  if (f->block_count + 1 > f->block_capacity) {
    f->block_capacity *= 2;
    if (f->block_capacity == 0)
      f->block_capacity = 1;
    block *new = realloc(f->blocks, sizeof(block) * f->block_capacity);
    if (new == NULL) {
      printf("Failed to allocate memory for block list\n");
      if (f->blocks != NULL)
        free(f->blocks);
      return NULL;
    }
    f->blocks = new;
  }

  block b = {0};
  b.id = f->block_count;
  f->blocks[f->block_count] = b;

  return f->blocks + (f->block_count++);
}

instruction *new_instruction(function *f, block *b) {
  if (f == NULL || b == NULL)
    return NULL;

  if (b->instruction_count + 1 > b->instruction_capacity) {
    b->instruction_capacity *= 2;
    if (b->instruction_capacity == 0)
      b->instruction_capacity = 1;
    instruction *new =
        realloc(b->instructions, sizeof(instruction) * b->instruction_capacity);
    if (new == NULL) {
      printf("Failed to allocate memory for instruction list\n");
      if (b->instructions != NULL)
        free(b->instructions);
      return NULL;
    }
    b->instructions = new;
  }

  instruction inst = {0};
  inst.dst = f->next_value_id++;
  b->instructions[b->instruction_count] = inst;

  return b->instructions + (b->instruction_count++);
}

type_def pointer_to(type_def *base) {
  type_def t = {0};
  t.kind = TY_PTR;
  t.base = base;
  return t;
}

void clean_module(module *mod) {
  for (int i = 0; i < mod->function_count; i++) {
    function f = mod->functions[i];
    free(f.name);
    clean_type(f.ret_type);
    for (int j = 0; j < f.argc; j++) {
      clean_type(f.arg_types[j]);
    }
    if (f.arg_types != NULL)
      free(f.arg_types);
    if (f.arg_values != NULL)
      free(f.arg_values);
    for (int j = 0; j < f.block_count; j++) {
      block b = f.blocks[j];
      free(b.label);
      for (int k = 0; k < b.instruction_count; k++) {
        instruction inst = b.instructions[k];
        clean_type(inst.ret);
        if (inst.op == IR_CALL) {
          free(inst.call.func);
          clean_type(inst.call.type);
          if (inst.call.args != NULL)
            free(inst.call.args);
        }
      }
      free(b.instructions);
    }
    if (f.blocks != NULL)
      free(f.blocks);
  }
  free(mod->functions);
}

void clean_type(type_def t) {
  if (t.kind == TY_PTR) {
    clean_type(*t.base);
    free(t.base);
  }
}

void print_type(type_def t) {
  switch (t.kind) {
  case TY_I64:
    printf("i64");
    break;
  case TY_I32:
    printf("i32");
    break;
  case TY_I16:
    printf("i16");
    break;
  case TY_I8:
    printf("i8");
    break;
  case TY_PTR:
    printf("*");
    print_type(*t.base);
    break;
  case TY_VOID:
    printf("void");
    break;
  }
}

void print_inst(instruction i) {
  switch (i.op) {
  case IR_ADD:
    printf("\t%%%d = add %%%d, %%%d\n", i.dst, i.binop.lhs, i.binop.rhs);
    break;
  case IR_SUB:
    printf("\t%%%d = sub %%%d, %%%d\n", i.dst, i.binop.lhs, i.binop.rhs);
    break;
  case IR_MUL:
    printf("\t%%%d = mul %%%d, %%%d\n", i.dst, i.binop.lhs, i.binop.rhs);
    break;
  case IR_SDIV:
    printf("\t%%%d = sdiv %%%d, %%%d\n", i.dst, i.binop.lhs, i.binop.rhs);
    break;
  case IR_SREM:
    printf("\t%%%d = srem %%%d, %%%d\n", i.dst, i.binop.lhs, i.binop.rhs);
    break;
  case IR_CONST:
    printf("\t%%%d = const ", i.dst);
    print_type(i.ret);
    printf(" %ld\n", i.constant);
    break;
  case IR_ALLOCA:
    printf("\t%%%d = alloca ", i.dst);
    if (i.alloca.count > 1)
      printf("[%d x ", i.alloca.count);
    print_type(i.alloca.type);
    if (i.alloca.count > 1)
      printf("]");
    printf("\n");
    break;
  case IR_STORE:
    printf("\t%%%d = store %%%d, %%%d\n", i.dst, i.binop.lhs, i.binop.rhs);
    break;
  case IR_LOAD:
    printf("\t%%%d = load %%%d\n", i.dst, i.value);
    break;
  case IR_RET:
    if (i.optional.present == 1) {
      printf("\t%%%d = ret %%%d\n", i.dst, i.optional.value);
    } else {
      printf("\t%%%d = ret void\n", i.dst);
    }
    break;
  case IR_CALL:
    printf("\t%%%d = call ", i.dst);
    print_type(i.call.type);
    printf("@%s(", i.call.func);
    for (int j = 0; j < i.call.argc; j++) {
      printf("%%%d", i.call.args[j]);
      if (j != i.call.argc - 1)
        printf(", ");
    }
    printf(")\n");
    break;
  case IR_NOT:
    printf("\t%%%d = not %%%d\n", i.dst, i.value);
    break;
  case IR_ADDR:
    printf("\t%%%d = ptr %%%d\n", i.dst, i.value);
    break;
  case IR_LOAD_ADDR:
    printf("\t%%%d = load:ptr %%%d\n", i.dst, i.value);
    break;
  case IR_STORE_ADDR:
    printf("\t%%%d = store:ptr %%%d\n", i.dst, i.value);
    break;
  }
}

void print_text_repr(module *mod) {
  for (int i = 0; i < mod->function_count; i++) {
    function f = mod->functions[i];
    printf("func ");
    print_type(f.ret_type);
    printf(" %s@%d(", f.name, f.id);
    for (int j = 0; j < f.argc; j++) {
      print_type(f.arg_types[j]);
      printf(" %%%d", f.arg_values[j]);
      if (j != f.argc - 1)
        printf(", ");
    }
    printf(") {\n");
    for (int j = 0; j < f.block_count; j++) {
      block b = f.blocks[j];
      printf("%s#%d:\n", b.label, b.id);
      for (int k = 0; k < b.instruction_count; k++) {
        print_inst(b.instructions[k]);
      }
    }
    printf("}\n");
  }
}

void remove_instuction(function *f, value_id rem) {
  for (int i = 0; i < f->block_count; i++) {
    block *b = &f->blocks[i];
    for (int j = 0; j < b->instruction_count; j++) {
      instruction *i = &b->instructions[j];
      if (i->dst == rem) {
        clean_type(i->ret);
        if (i->op == IR_CALL) {
          free(i->call.func);
          if (i->call.args != NULL)
            free(i->call.args);
        }
        for (; j < b->instruction_count - 1; j++) {
          b->instructions[j] = b->instructions[j + 1];
        }
        b->instruction_count--;
        f->changed = 1;
        return;
      }
    }
  }
}

int recalculate_ids(function *f) {
  value_id *mapto = malloc(sizeof(value_id) * (f->next_value_id));
  if (mapto == NULL) {
    return -1;
  }

  int counter = 0;
  for (int i = 0; i < f->argc; i++) {
    mapto[f->arg_values[i]] = counter++;
  }
  for (int i = 0; i < f->block_count; i++) {
    block b = f->blocks[i];
    for (int j = 0; j < b.instruction_count; j++) {
      instruction i = b.instructions[j];
      mapto[i.dst] = counter++;
    }
  }
  if (counter == f->next_value_id) {
    free(mapto);
    return 0;
  }
  f->next_value_id = counter;

  for (int i = 0; i < f->block_count; i++) {
    block *b = &f->blocks[i];
    for (int j = 0; j < b->instruction_count; j++) {
      instruction *i = &b->instructions[j];
      i->dst = mapto[i->dst];
      switch (i->op) {
      case IR_ADD:
      case IR_SUB:
      case IR_MUL:
      case IR_SDIV:
      case IR_SREM:
      case IR_STORE:
      case IR_STORE_ADDR:
        i->binop.lhs = mapto[i->binop.lhs];
        i->binop.rhs = mapto[i->binop.rhs];
        break;
      case IR_LOAD:
      case IR_NOT:
      case IR_ADDR:
      case IR_LOAD_ADDR:
        i->value = mapto[i->value];
        break;
      case IR_CALL:
        for (int iter = 0; iter < i->call.argc; iter++)
          i->call.args[iter] = mapto[i->call.args[iter]];
        break;
      case IR_RET:
        if (i->optional.present)
          i->optional.value = mapto[i->optional.value];
        break;
      case IR_CONST:
      case IR_ALLOCA:
        break;
      }
    }
  }
  free(mapto);

  f->changed = 1;

  return 0;
}
