#include "compiler.h"
#include "ir.h"

#include "stdio.h"
#include "stdlib.h"
#include "string.h"

const char *const reg_names_64[] = {"rax", "rbx", "rcx", "rdx",
                                    "rsi", "rdi", "r8",  "r9"};
const char *const reg_names_32[] = {"eax", "ebx", "ecx", "edx",
                                    "esi", "edi", "r8d", "r9d"};

const int arg_regs[] = {REG_RDI, REG_RSI, REG_RDX, REG_RCX, REG_R8, REG_R9};

// Appends null-terminated string to generated assembly
int append(compiler_state *state, char *text) {
  int len = strlen(text);

  char *new =
      realloc(state->generated, sizeof(char) * (state->output_len + len));
  if (new == NULL) {
    if (state->generated != NULL)
      free(state->generated);
    printf("Failed to reallocate space for generated assembly\n");
    return -1;
  }
  state->generated = new;
  memcpy(state->generated + state->output_len, text, len);
  state->output_len += len;
  return len;
}

int emit_prologue(compiler_state *state) {
  return append(state, "section .text\nglobal "
                       "_start\n_start:\n\tcall main\n\n\tmov rdi, rax ; exit "
                       "code\n\tmov rax, 60 ; sys_exit\n\tsyscall\n\n");
}

int is_pure(opcode op) {
  return op == IR_ADD || op == IR_SUB || op == IR_MUL || op == IR_SDIV ||
         op == IR_SREM || op == IR_CONST || op == IR_LOAD;
}

int dead_value_elim(function *func) {
  int *uses = calloc(func->next_value_id, sizeof(int));
  if (uses == NULL)
    return -1;

  for (int i = 0; i < func->block_count; i++) {
    block b = func->blocks[i];
    for (int j = 0; j < b.instruction_count; j++) {
      instruction i = b.instructions[j];
      if (!is_pure(i.op))
        uses[i.dst]++;
      switch (i.op) {
      case IR_ADD:
      case IR_SUB:
      case IR_MUL:
      case IR_SDIV:
      case IR_SREM:
      case IR_STORE:
        uses[i.binop.lhs]++;
        uses[i.binop.rhs]++;
        break;
      case IR_LOAD:
        uses[i.value]++;
        break;
      case IR_CALL:
        for (int iter = 0; iter < i.call.argc; iter++)
          uses[i.call.args[iter]]++;
        break;
      case IR_RET:
        if (i.optional.present)
          uses[i.optional.value]++;
        break;
      default:
        break;
      }
    }
  }

  for (int i = 0; i < func->next_value_id; i++) {
    if (uses[i] == 0) {
      remove_instuction(func, i);
    }
  }

  recalculate_ids(func);

  free(uses);
  return 0;
}

void remove_dependant(function *func, value_id dep) {
  for (int i = 0; i < func->block_count; i++) {
    block b = func->blocks[i];
    for (int j = 0; j < b.instruction_count; j++) {
      instruction i = b.instructions[j];
      int is_dependant = 0;
      switch (i.op) {
      case IR_ADD:
      case IR_SUB:
      case IR_MUL:
      case IR_SDIV:
      case IR_SREM:
      case IR_STORE:
        if (i.binop.lhs == dep || i.binop.rhs == dep)
          is_dependant = 1;

        break;
      case IR_LOAD:
        if (i.value == dep)
          is_dependant = 1;
        break;
      case IR_CALL:
        for (int iter = 0; iter < i.call.argc; iter++)
          if (i.call.args[iter] == dep)
            is_dependant = 1;
        break;
      case IR_RET:
        if (i.optional.present)
          if (i.optional.value == dep)
            is_dependant = 1;
        break;
      default:
        break;
      }

      if (is_dependant) {
        remove_instuction(func, i.dst);
        remove_dependant(func, i.dst);
      }
    }
  }
}

int dead_store_elim(function *func) {
  mem_info *info = calloc(func->next_value_id, sizeof(mem_info));
  if (info == NULL)
    return -1;

  int counter = 0;
  for (int i = 0; i < func->block_count; i++) {
    block b = func->blocks[i];
    for (int j = 0; j < b.instruction_count; j++) {
      instruction i = b.instructions[j];
      switch (i.op) {
      case IR_ALLOCA:
        info[i.dst].is_alloca = 1;
        break;
      case IR_STORE:
        info[i.binop.lhs].stores++;
        break;
      case IR_LOAD:
        info[i.value].loads++;
        break;
      default:
        break;
      }
    }
  }

  for (int i = 0; i < func->next_value_id; i++) {
    if (info[i].is_alloca && info[i].loads == 0) {
      remove_dependant(func, i);
      remove_instuction(func, i);
    }
  }

  recalculate_ids(func);

  return 0;
}

int _sizeof(type_def t) {
  switch (t.kind) {
  case TY_I64:
  case TY_PTR:
    return 8;
  case TY_I32:
    return 4;
  case TY_VOID:
    return 0;
  }
}

int align_up(int current, int allignment) {
  return current + (current % allignment);
}

int emit_function_prologue(compiler_state *state, function *f) {
  if (state->slots != NULL)
    free(state->slots);
  if (state->value_loc != NULL)
    free(state->value_loc);

  state->slots = calloc(f->next_value_id, sizeof(stack_slot));
  if (state->slots == NULL) {
    return -1;
  }
  state->value_loc = calloc(f->next_value_id, sizeof(location));
  if (state->value_loc == NULL) {
    free(state->slots);
    return -1;
  }
  state->valc = f->next_value_id;

  int offset = 0;
  int size = 0;
  for (int i = 0; i < f->argc; i++) {
    size = _sizeof(f->arg_types[i]);
    offset = align_up(offset, size);
    offset += size;
    state->slots[f->arg_values[i]].size = size;
    state->slots[f->arg_values[i]].offset = offset;
  }
  for (int i = 0; i < f->block_count; i++) {
    block b = f->blocks[i];
    for (int j = 0; j < b.instruction_count; j++) {
      instruction i = b.instructions[j];
      switch (i.op) {
      case IR_ALLOCA:
        size = _sizeof(i.alloca.type);
        offset = align_up(offset, size);
        offset += size;
        state->slots[i.dst].size = size;
        state->slots[i.dst].offset = offset;
        break;
      default:
        size = _sizeof(i.ret);
        // Otherwise we run into 0 division
        if (size != 0) {
          offset = align_up(offset, size);
          offset += size;
          state->slots[i.dst].size = size;
          state->slots[i.dst].offset = offset;
        }
        break;
      }
    }
  }

  offset = align_up(offset, 16);

  char *buf;
  for (int i = 0; i < f->block_count; i++) {
    block b = f->blocks[i];
    int ret;
    if (b.label == NULL)
      ret = asprintf(&buf,
                     "%s:\n\tpush rbp\n\tmov rbp, "
                     "rsp\n\tsub rsp, %d\n\n",
                     f->name, offset);
    else
      ret = asprintf(&buf,
                     "%s_%s:\n\tpush rbp\n\tmov rbp, "
                     "rsp\n\tsub rsp, %d\n\n",
                     f->name, b.label, offset);
    if (ret < 0) {
      free(state->slots);
      return -1;
    }
    ret = append(state, buf);
    if (ret < 0) {
      free(state->slots);
    }
    free(buf);

    if (b.label == NULL) {
      for (int j = 0; j < f->argc; j++) {
        // BUG: Handle byte width properly (change register based on sizeof)
        ret = asprintf(
            &buf, "\tmov QWORD [rbp-%d], %s\n",
            state->slots[f->arg_values[j]].offset,
            reg_names_64[arg_regs[j]]); // TODO: Handle other args on stack
        if (ret == -1) {
          free(state->slots);
          return ret;
        }
        ret = append(state, buf);
        free(buf);
      }
    }
  }

  return 0;
}

int alloc_reg(compiler_state *state) {
  for (int i = 0; i < REG_COUNT; i++)
    if (state->reg_used[i] == 0) {
      state->reg_used[i] = 1;
      return i;
    }
  printf("Failed to find free register\n");
  return -1;
}

void free_reg(compiler_state *state, int reg) { state->reg_used[reg] = 0; }

int spill_register(compiler_state *state, int reg) {
  free_reg(state, reg);
  int id = -1;
  for (int i = 0; i < state->valc; i++) {
    if (state->value_loc[i].kind == LOC_REG && state->value_loc[i].reg == reg)
      id = i;
  }
  if (id == -1)
    return 0;

  location *loc = &state->value_loc[id];
  char *buf;
  int ret = asprintf(&buf, "\tmov QWORD [rbp-%d], %s\n",
                     state->slots[id].offset, reg_names_64[loc->reg]);
  if (ret < 0)
    return ret;
  ret = append(state, buf);
  free(buf);
  if (ret < 0)
    return ret;
  loc->kind = LOC_STACK;
  loc->stack = state->slots[id].offset;
  return 0;
}

int ensure_reg(compiler_state *state, value_id id) {
  location loc = state->value_loc[id];
  int tmp_reg;
  if (loc.kind == LOC_NONE) {
    printf(
        "PANIC: Value with id %%%d has not been stored yet but is trying to be "
        "accessed (shouldn't be possible, probably an issue with your IR)!\n",
        id);
    return -1;
  } else if (loc.kind == LOC_IMM) {
    tmp_reg = alloc_reg(state);
    if (tmp_reg < 0)
      return tmp_reg;
    char *buf;
    int ret =
        asprintf(&buf, "\tmov %s, %ld\n", reg_names_64[tmp_reg], loc.immediate);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
  } else if (loc.kind == LOC_STACK) {
    tmp_reg = alloc_reg(state);
    if (tmp_reg < 0)
      return tmp_reg;
    char *buf;
    int ret = asprintf(&buf, "\tmov %s, QWORD [rbp-%d]\n",
                       reg_names_64[tmp_reg], loc.stack);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
  } else if (loc.kind == LOC_REG) {
    tmp_reg = loc.reg;
  }
  return tmp_reg;
}

int ensure_in_reg(compiler_state *state, value_id id, int reg) {
  location *loc = &state->value_loc[id];

  if (loc->kind == LOC_REG && loc->reg == reg)
    return 0;
  if (loc->kind == LOC_NONE) {
    printf(
        "PANIC: Value with id %%%d has not been stored yet but is trying to be "
        "accessed (shouldn't be possible, probably an issue with your IR)!\n",
        id);
    return -1;
  }

  int ret = 0;
  if (state->reg_used[reg])
    ret = spill_register(state, reg);

  if (ret < 0)
    return ret;

  char *buf;
  if (loc->kind == LOC_REG) {
    ret = asprintf(&buf, "\tmov %s, %s\n", reg_names_64[reg],
                   reg_names_64[loc->reg]);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
  } else if (loc->kind == LOC_IMM) {
    ret = asprintf(&buf, "\tmov %s, %ld\n", reg_names_64[reg], loc->immediate);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
  } else {
    ret = asprintf(&buf, "\tmov %s, QWORD [rbp-%d]\n", reg_names_64[reg],
                   loc->stack);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
  }

  loc->kind = LOC_REG;
  loc->reg = reg;

  state->reg_used[reg] = 1;

  return ret;
}

int lower_instruction(compiler_state *state, instruction i) {
  int ret = 0;
  char *buf;
  location *loc;
  int tmp_reg, tmp_reg_2;
  // BUG: Handle register width properly
  switch (i.op) {
  case IR_ADD:
    tmp_reg = ensure_reg(state, i.binop.lhs);
    if (tmp_reg < 0)
      return tmp_reg;
    tmp_reg_2 = ensure_reg(state, i.binop.rhs);
    if (tmp_reg_2 < 0)
      return tmp_reg_2;

    ret = asprintf(&buf, "\tadd %s, %s\n", reg_names_64[tmp_reg],
                   reg_names_64[tmp_reg_2]);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg = tmp_reg;
    free_reg(state, tmp_reg_2);
    break;
  case IR_SUB:
    tmp_reg = ensure_reg(state, i.binop.lhs);
    if (tmp_reg < 0)
      return tmp_reg;
    tmp_reg_2 = ensure_reg(state, i.binop.rhs);
    if (tmp_reg_2 < 0)
      return tmp_reg_2;

    ret = asprintf(&buf, "\tsub %s, %s\n", reg_names_64[tmp_reg],
                   reg_names_64[tmp_reg_2]);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg = tmp_reg;
    free_reg(state, tmp_reg_2);
    break;
  case IR_MUL:
    tmp_reg = ensure_reg(state, i.binop.lhs);
    if (tmp_reg < 0)
      return tmp_reg;
    tmp_reg_2 = ensure_reg(state, i.binop.rhs);
    if (tmp_reg_2 < 0)
      return tmp_reg_2;

    ret = asprintf(&buf, "\tmul %s, %s\n", reg_names_64[tmp_reg],
                   reg_names_64[tmp_reg_2]);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg = tmp_reg;
    free_reg(state, tmp_reg_2);
    break;
  case IR_SDIV:
    ret = spill_register(state, REG_RDX);
    if (ret < 0)
      return ret;
    ret = ensure_in_reg(state, i.binop.lhs, REG_RAX);
    if (ret < 0)
      return ret;
    ret = append(state, "\tcqo\n");
    if (ret < 0)
      return ret;
    state->reg_used[REG_RDX] = 1;
    tmp_reg = ensure_reg(state, i.binop.rhs);

    ret = asprintf(&buf, "\tidivq %s\n", reg_names_64[tmp_reg]);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg = REG_RAX;
    free_reg(state, tmp_reg);
    free_reg(state, REG_RDX);
    break;
  case IR_SREM:
    ret = spill_register(state, REG_RDX);
    if (ret < 0)
      return ret;
    ret = ensure_in_reg(state, i.binop.lhs, REG_RAX);
    if (ret < 0)
      return ret;
    ret = append(state, "\tcqo\n");
    if (ret < 0)
      return ret;
    state->reg_used[REG_RDX] = 1;
    tmp_reg = ensure_reg(state, i.binop.rhs);

    ret = asprintf(&buf, "\tidiv %s\n", reg_names_64[tmp_reg]);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg = REG_RDX;
    free_reg(state, tmp_reg);
    free_reg(state, REG_RAX);
    break;
  case IR_ALLOCA:
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_STACK;
    loc->stack = state->slots[i.dst].offset;
    break;
  case IR_CONST:
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_IMM;
    loc->immediate = i.constant;
    break;
  case IR_STORE:
    loc = &state->value_loc[i.binop.rhs];
    if (loc->kind == LOC_IMM) {
      ret = asprintf(&buf, "\tmov QWORD [rbp-%d], %ld\n",
                     state->slots[i.binop.lhs].offset, loc->immediate);
      if (ret < 0)
        return ret;
      ret = append(state, buf);
      free(buf);
      if (ret < 0)
        return ret;
    } else {
      tmp_reg = ensure_reg(state, i.binop.rhs);
      if (tmp_reg < 0)
        return tmp_reg;
      ret = asprintf(&buf, "\tmov QWORD [rbp-%d], %s\n",
                     state->slots[i.binop.lhs].offset, reg_names_64[tmp_reg]);
      if (ret < 0)
        return ret;
      ret = append(state, buf);
      free(buf);
      if (ret < 0)
        return ret;
      free_reg(state, tmp_reg);
    }
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_STACK;
    loc->stack = state->slots[i.dst].offset;
    break;
  case IR_LOAD:
    tmp_reg = ensure_reg(state, i.value);
    if (tmp_reg < 0)
      return tmp_reg;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg = tmp_reg;
    break;
  case IR_CALL:
    for (int j = 0; j < i.call.argc; j++) {
      ret = ensure_in_reg(
          state, i.call.args[j],
          arg_regs[j]); // TODO: Handle pushing additional args to stack
      if (ret < 0)
        return ret;
    }

    // TODO: Once pushing is implemented, handle stack alignment
    char *func_name = NULL;
    for (int j = 0; j < state->mod->function_count; j++)
      if (state->mod->functions[j].id == i.call.func)
        func_name = state->mod->functions[j].name;
    if (func_name == NULL)
      return -1;

    ret = asprintf(&buf, "\tcall %s\n", func_name);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;

    // TODO: Clear stack of extra args

    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg = REG_RAX;
    break;
  case IR_RET:
    if (i.optional.present) {
      tmp_reg = ensure_reg(state, i.optional.value);
      if (tmp_reg < 0)
        return tmp_reg;
      if (tmp_reg != REG_RAX) {
        ret = asprintf(&buf, "\tmov %s, %s\n", reg_names_64[REG_RAX],
                       reg_names_64[tmp_reg]);
        if (ret < 0)
          return ret;
        ret = append(state, buf);
        free(buf);
        if (ret < 0)
          return ret;
      }
    }
    ret = append(state, "\n\tmov rsp, rbp\n\tpop rbp\n\tret\n\n");
    break;
  }
  return ret;
}

int generate_asm(module *mod, char **output) {
  compiler_state state = {0};
  state.mod = mod;
  int ret = emit_prologue(&state);
  if (ret < 0) {
    printf("Failed to emit program epilogue\n");
    free(state.generated);
    return ret;
  }

  for (int i = 0; i < mod->function_count; i++) {
    function *f = &mod->functions[i];
    // Dead code analysis
    for (f->changed = 1; f->changed == 1;) {
      f->changed = 0;
      ret = dead_value_elim(f);
      if (ret < 0) {
        free(state.generated);
        return ret;
      }
      ret = dead_store_elim(f);
      if (ret < 0) {
        free(state.generated);
        return ret;
      }
    }
    // TODO: Remove
    printf("Post optimization:\n");
    print_text_repr(mod);

    ret = emit_function_prologue(&state, f);
    if (ret < 0) {
      free(state.generated);
      return ret;
    }

    for (int j = 0; j < f->block_count; j++) {
      block b = f->blocks[j];
      for (int k = 0; k < b.instruction_count; k++) {
        instruction inst = b.instructions[k];
        ret = lower_instruction(&state, inst);
        if (ret < 0) {
          free(state.generated);
          return ret;
        }
      }
    }
  }

  *output = state.generated;
  free(state.slots);
  free(state.value_loc);
  return state.output_len;
}
