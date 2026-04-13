#include "compiler.h"
#include "ir.h"

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include <stddef.h>
#include <stdint.h>

const char *const reg_names_64[] = {"rax", "rbx", "rcx", "rdx", "rsi",
                                    "rdi", "r8",  "r9",  "r10", "r11",
                                    "r12", "r13", "r14", "r15"};
const char *const reg_names_32[] = {"eax",  "ebx",  "ecx",  "edx",  "esi",
                                    "edi",  "r8d",  "r9d",  "r10d", "r11d",
                                    "r12d", "r13d", "r14d", "r15d"};
const char *const reg_names_16[] = {"ax",   "bx",   "cx",   "dx",   "si",
                                    "di",   "r8w",  "r9w",  "r10w", "r11w",
                                    "r12w", "r13w", "r14w", "r15w"};
const char *const reg_names_8[] = {"al",   "bl",   "cl",   "dl",   "sil",
                                   "dil",  "r8b",  "r9b",  "r10b", "r11b",
                                   "r12b", "r13b", "r14b", "r15b"};

#define N_ARG_REGISTERS 6
const int arg_regs[N_ARG_REGISTERS] = {REG_RDI, REG_RSI, REG_RDX,
                                       REG_RCX, REG_R8,  REG_R9};

// Registers that should be preserved across function calls (according to
// https://stackoverflow.com/a/18024743)
#define N_PRESERVED_REGS 5
const int preserved_regs[N_PRESERVED_REGS] = {REG_RBX, REG_R12, REG_R13,
                                              REG_R14, REG_R15};

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

int append_at(compiler_state *state, char *text, int location) {
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
  memmove(state->generated + location + len, state->generated + location,
          state->output_len - location);
  memcpy(state->generated + location, text, len);
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
      case IR_STORE_ADDR:
      case IR_EQ:
      case IR_NE:
      case IR_LT:
      case IR_GT:
      case IR_LE:
      case IR_GE:
        uses[i.binop.lhs]++;
        uses[i.binop.rhs]++;
        break;
      case IR_LOAD:
      case IR_NOT:
      case IR_ADDR:
      case IR_LOAD_ADDR:
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
      case IR_CONST:
      case IR_ALLOCA:
      case IR_JMP:
        break;
      case IR_BRANCH:
        uses[i.branch.cond]++;
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
      case IR_STORE_ADDR:
      case IR_EQ:
      case IR_NE:
      case IR_LT:
      case IR_GT:
      case IR_LE:
      case IR_GE:
        if (i.binop.lhs == dep || i.binop.rhs == dep)
          is_dependant = 1;

        break;
      case IR_LOAD:
      case IR_NOT:
      case IR_ADDR:
      case IR_LOAD_ADDR:
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
      case IR_CONST:
      case IR_ALLOCA:
      case IR_JMP:
        break;
      case IR_BRANCH:
        if (i.branch.cond == dep)
          is_dependant = 1;
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
      case IR_ADDR:
        // BUG: Treats a reference as if it was used by default
        info[i.value].loads++;
        info[i.value].stores++;
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

uint32_t hash_string(const char *s) {
  uint32_t hash = 0;

  for (; *s; ++s) {
    hash += *s;
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }

  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);

  return hash;
}

int array_contains(uint32_t *arr, size_t len, uint32_t elem) {
  if (len == 0)
    return 0;
  for (; *arr; arr++)
    if (*arr == elem)
      return 1;
  return 0;
}

int dead_branch_elim(function *func) {
  uint32_t *labels = NULL;
  size_t len = 0;

  for (int i = 0; i < func->block_count; i++) {
    block b = func->blocks[i];
    for (int j = 0; j < b.instruction_count; j++) {
      instruction inst = b.instructions[j];
      switch (inst.op) {
      case IR_JMP:
        if (!array_contains(labels, len, hash_string(inst.label))) {
          len++;
          uint32_t *new = realloc(labels, sizeof(uint32_t) * len);
          if (new == NULL) {
            free(labels);
            return -1;
          }
          labels = new;
          labels[len - 1] = hash_string(inst.label);
        }
        break;
      case IR_BRANCH:
        if (!array_contains(labels, len, hash_string(inst.branch.lfalse))) {
          len++;
          uint32_t *new = realloc(labels, sizeof(uint32_t) * len);
          if (new == NULL) {
            free(labels);
            return -1;
          }
          labels = new;
          labels[len - 1] = hash_string(inst.branch.lfalse);
        }
        if (!array_contains(labels, len, hash_string(inst.branch.ltrue))) {
          len++;
          uint32_t *new = realloc(labels, sizeof(uint32_t) * len);
          if (new == NULL) {
            free(labels);
            return -1;
          }
          labels = new;
          labels[len - 1] = hash_string(inst.branch.ltrue);
        }
        break;
      default:
        break;
      }
    }
  }

  if ((int)(len + 1) != func->block_count) {
    func->changed = 1;
    block *new_blocks = malloc(sizeof(block) * (len + 1)); // +1 for entry
    if (new_blocks == NULL)
      return -1;
    int nb_index = 0;
    for (int i = 0; i < func->block_count; i++) {
      block b = func->blocks[i];
      if (b.label == NULL || array_contains(labels, len, hash_string(b.label)))
        new_blocks[nb_index++] = b;
    }
    free(func->blocks);
    func->blocks = new_blocks;
  }
  return 0;
}

int _sizeof(type_def t) {
  switch (t.kind) {
  case TY_I64:
  case TY_PTR:
    return 8;
  case TY_I32:
    return 4;
  case TY_I16:
    return 2;
  case TY_I8:
    return 1;
  case TY_VOID:
    return 0;
  }
  return 0;
}

static inline const char *get_width(int reg, int width) {
  switch (width) {
  case 1:
    return reg_names_8[reg];
  case 2:
    return reg_names_16[reg];
  case 4:
    return reg_names_32[reg];
  case 8:
    return reg_names_64[reg];
  default:
    printf("PANIC: Trying to get register with index %d of width %d\n", reg,
           width);
    exit(1);
  }
}

const char *const specifiers[] = {"", "WORD", "DWORD", "QWORD"};
static inline const char *width_specifier(int width) {
  switch (width) {
  case 1:
    return specifiers[0];
  case 2:
    return specifiers[1];
  case 4:
    return specifiers[2];
  case 8:
    return specifiers[3];
  default:
    printf("PANIC: Trying to get width specifier for %d bytes\n", width);
    exit(1);
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

  for (int i = 0; i < REG_COUNT; i++)
    state->reg_used[i] = state->was_modified[i] = 0;

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

    state->fprologue_end = state->output_len;

    if (b.label == NULL) {
      for (int j = 0; j < f->argc; j++) {
        location *loc = &state->value_loc[j];
        loc->kind = LOC_REG;
        loc->reg.id = arg_regs[j];
        loc->reg.width = state->slots[j].size;
      }
    }
  }

  return 0;
}

int alloc_reg(compiler_state *state) {
  for (int i = 0; i < REG_COUNT; i++)
    if (state->reg_used[i] == 0) {
      state->reg_used[i] = state->was_modified[i] = 1;
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
    if (state->value_loc[i].kind == LOC_REG &&
        state->value_loc[i].reg.id == reg)
      id = i;
  }
  if (id == -1)
    return 0;

  location *loc = &state->value_loc[id];
  char *buf;
  int ret = asprintf(
      &buf, "\tmov %s [rbp-%d], %s\n", width_specifier(state->slots[id].size),
      state->slots[id].offset, get_width(loc->reg.id, loc->reg.width));
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
  location *loc = &state->value_loc[id];
  int width = state->slots[id].size;
  int tmp_reg;
  if (loc->kind == LOC_NONE) {
    printf(
        "PANIC: Value with id %%%d has not been stored yet but is trying to be "
        "accessed (shouldn't be possible, probably an issue with your IR)!\n",
        id);
    return -1;
  } else if (loc->kind == LOC_IMM) {
    tmp_reg = alloc_reg(state);
    if (tmp_reg < 0)
      return tmp_reg;
    char *buf;
    int ret = asprintf(&buf, "\tmov %s, %ld\n", get_width(tmp_reg, width),
                       loc->immediate);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
  } else if (loc->kind == LOC_STACK) {
    tmp_reg = alloc_reg(state);
    if (tmp_reg < 0)
      return tmp_reg;
    char *buf;
    int ret =
        asprintf(&buf, "\tmov %s, %s [rbp-%d]\n", get_width(tmp_reg, width),
                 width_specifier(width), loc->stack);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
  } else if (loc->kind == LOC_REG) {
    tmp_reg = loc->reg.id;
  }

  loc->kind = LOC_REG;
  loc->reg.id = tmp_reg;
  loc->reg.width = width;

  return tmp_reg;
}

int ensure_in_reg(compiler_state *state, value_id id, int reg) {
  location *loc = &state->value_loc[id];
  int width = state->slots[id].size;

  state->was_modified[reg] = 1;

  if (loc->kind == LOC_REG && loc->reg.id == reg)
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
    ret = asprintf(&buf, "\tmov %s, %s\n", get_width(reg, width),
                   get_width(loc->reg.id, loc->reg.width));
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    free_reg(state, loc->reg.id);
  } else if (loc->kind == LOC_IMM) {
    ret = asprintf(&buf, "\tmov %s, %ld\n", get_width(reg, width),
                   loc->immediate);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
  } else {
    ret = asprintf(&buf, "\tmov %s, %s [rbp-%d]\n", width_specifier(width),
                   get_width(reg, width), loc->stack);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
  }

  loc->kind = LOC_REG;
  loc->reg.id = reg;
  loc->reg.width = width;

  state->reg_used[reg] = 1;

  return ret;
}

int lower_instruction(compiler_state *state, instruction i) {
  int ret = 0;
  char *buf;
  location *loc;
  int tmp_reg, tmp_reg_2, width;
  // BUG: Handle register width properly
  switch (i.op) {
  case IR_ADD:
    tmp_reg = ensure_reg(state, i.binop.lhs);
    if (tmp_reg < 0)
      return tmp_reg;
    tmp_reg_2 = ensure_reg(state, i.binop.rhs);
    if (tmp_reg_2 < 0)
      return tmp_reg_2;

    width = state->slots[i.binop.lhs].size;

    ret = asprintf(&buf, "\tadd %s, %s\n", get_width(tmp_reg, width),
                   get_width(tmp_reg_2, width));
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = tmp_reg;
    loc->reg.width = state->slots[i.binop.lhs].size;
    free_reg(state, tmp_reg_2);
    break;
  case IR_SUB:
    tmp_reg = ensure_reg(state, i.binop.lhs);
    if (tmp_reg < 0)
      return tmp_reg;
    tmp_reg_2 = ensure_reg(state, i.binop.rhs);
    if (tmp_reg_2 < 0)
      return tmp_reg_2;

    width = state->slots[i.binop.lhs].size;

    ret = asprintf(&buf, "\tsub %s, %s\n", get_width(tmp_reg, width),
                   get_width(tmp_reg_2, width));
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = tmp_reg;
    loc->reg.width = state->slots[i.binop.lhs].size;
    free_reg(state, tmp_reg_2);
    break;
  case IR_MUL:
    tmp_reg = ensure_reg(state, i.binop.lhs);
    if (tmp_reg < 0)
      return tmp_reg;
    tmp_reg_2 = ensure_reg(state, i.binop.rhs);
    if (tmp_reg_2 < 0)
      return tmp_reg_2;

    width = state->slots[i.binop.lhs].size;

    ret = asprintf(&buf, "\tmul %s, %s\n", get_width(tmp_reg, width),
                   get_width(tmp_reg_2, width));
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = tmp_reg;
    loc->reg.width = state->slots[i.binop.lhs].size;
    free_reg(state, tmp_reg_2);
    break;
  case IR_SDIV:
    ret = spill_register(state, REG_RDX);
    if (ret < 0)
      return ret;
    ret = ensure_in_reg(state, i.binop.lhs, REG_RAX);
    if (ret < 0)
      return ret;
    width = state->slots[i.binop.lhs].size;
    if (width == 8)
      ret = append(state, "\tcqo\n");
    else if (width == 4)
      ret = append(state, "\tCDQ\n");
    else if (width == 2)
      ret = append(state, "\tCWD\n");
    if (ret < 0)
      return ret;
    state->reg_used[REG_RDX] = state->was_modified[REG_RDX] = 1;
    tmp_reg = ensure_reg(state, i.binop.rhs);

    ret = asprintf(&buf, "\tidiv %s\n", get_width(tmp_reg, width));
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = REG_RAX;
    loc->reg.width = width;
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
    width = state->slots[i.binop.lhs].size;
    if (width == 8)
      ret = append(state, "\tcqo\n");
    else if (width == 4)
      ret = append(state, "\tCDQ\n");
    else if (width == 2)
      ret = append(state, "\tCWD\n");
    if (ret < 0)
      return ret;
    state->reg_used[REG_RDX] = state->was_modified[REG_RDX] = 1;
    tmp_reg = ensure_reg(state, i.binop.rhs);

    ret = asprintf(&buf, "\tidiv %s\n", get_width(tmp_reg, width));
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = REG_RDX;
    loc->reg.width = width;
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
    width = state->slots[i.binop.lhs].size;
    if (loc->kind == LOC_IMM) {
      ret = asprintf(&buf, "\tmov %s [rbp-%d], %ld\n", width_specifier(width),
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
      ret =
          asprintf(&buf, "\tmov %s [rbp-%d], %s\n", width_specifier(width),
                   state->slots[i.binop.lhs].offset, get_width(tmp_reg, width));
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
    loc->reg.id = tmp_reg;
    loc->reg.width = state->slots[i.value].size;
    break;
  case IR_CALL:
    for (int j = 0; j < i.call.argc; j++) {
      ret = ensure_in_reg(
          state, i.call.args[j],
          arg_regs[j]); // TODO: Handle pushing additional args to stack
      if (ret < 0)
        return ret;
    }

    // Spill caller saved registers
    for (int j = 0; j < REG_COUNT; j++) {
      if (state->reg_used[j]) {
        int needs_spilling = 1;
        for (int k = 0; k < N_PRESERVED_REGS; k++)
          if (preserved_regs[k] == j)
            needs_spilling = 0;
        if (!needs_spilling)
          continue;
        for (int k = 0; k < N_ARG_REGISTERS && k < i.call.argc; k++)
          if (arg_regs[k] == j)
            needs_spilling = 0;
        if (needs_spilling)
          ret = spill_register(state, j);
        if (ret < 0)
          return ret;
      }
    }

    // TODO: Once pushing is implemented, handle stack alignment
    ret = asprintf(&buf, "\tcall %s\n", i.call.func);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;

    for (int j = 0; j < i.call.argc; j++) {
      free_reg(state, arg_regs[j]);
    }

    // TODO: Clear stack of extra args

    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = REG_RAX;
    loc->reg.width = _sizeof(i.call.type);
    break;
  case IR_RET:
    if (i.optional.present) {
      tmp_reg = ensure_reg(state, i.optional.value);
      if (tmp_reg < 0)
        return tmp_reg;
      width = state->slots[i.optional.value].size;
      if (tmp_reg != REG_RAX) {
        ret = asprintf(&buf, "\tmov %s, %s\n", get_width(REG_RAX, width),
                       get_width(tmp_reg, width));
        if (ret < 0)
          return ret;
        ret = append(state, buf);
        free(buf);
        if (ret < 0)
          return ret;
      }
    }

    int to_preserve = 0;
    for (int i = 0; i < N_PRESERVED_REGS; i++) {
      if (state->was_modified[preserved_regs[i]]) {
        if (to_preserve == 0) {
          ret = append(state, "\n");
          if (ret < 0)
            return ret;
          ret = append_at(state, "\n", state->fprologue_end);
          to_preserve++;
        }
        ret = asprintf(&buf, "\tpush %s\n", reg_names_64[preserved_regs[i]]);
        if (ret < 0)
          return ret;
        ret = append_at(state, buf, state->fprologue_end);
        free(buf);
        if (ret < 0)
          return ret;
        ret = asprintf(&buf, "\tpop %s\n", reg_names_64[preserved_regs[i]]);
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
  case IR_NOT:
    tmp_reg = ensure_reg(state, i.value);
    if (tmp_reg < 0)
      return tmp_reg;

    width = state->slots[i.value].size;

    ret = asprintf(&buf, "\tcmp %s, 0\n\tsete al\n", get_width(tmp_reg, width));
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    if (width > 1) {
      ret = asprintf(&buf, "\tmovzx %s, al\n", get_width(tmp_reg, width));
      if (ret < 0)
        return ret;
      ret = append(state, buf);
      free(buf);
      if (ret < 0)
        return ret;
    }
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = tmp_reg;
    loc->reg.width = state->slots[i.value].size;
    break;
  case IR_ADDR:
    tmp_reg = alloc_reg(state);
    if (tmp_reg < 0)
      return tmp_reg;
    ret = asprintf(&buf, "\tlea %s, [rbp-%d]\n", reg_names_64[tmp_reg],
                   state->slots[i.value].offset);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = tmp_reg;
    loc->reg.width = 8;
    break;
  case IR_STORE_ADDR:
    loc = &state->value_loc[i.binop.rhs];
    width = state->slots[i.binop.rhs].size;
    tmp_reg_2 = ensure_reg(state, i.binop.lhs);
    if (loc->kind == LOC_IMM) {
      ret = asprintf(&buf, "\tmov %s [%s], %ld\n", width_specifier(width),
                     reg_names_64[tmp_reg_2], loc->immediate);
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
      ret = asprintf(&buf, "\tmov %s [%s], %s\n", width_specifier(width),
                     reg_names_64[tmp_reg_2], get_width(tmp_reg, width));
      if (ret < 0)
        return ret;
      ret = append(state, buf);
      free(buf);
      if (ret < 0)
        return ret;
      free_reg(state, tmp_reg);
    }
    free_reg(state, tmp_reg_2);
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_STACK;
    loc->stack = state->slots[i.dst].offset;
    break;
  case IR_LOAD_ADDR:
    tmp_reg = ensure_reg(state, i.value);
    if (tmp_reg < 0)
      return tmp_reg;
    width = _sizeof(i.ret);
    ret = asprintf(&buf, "\tmov %s, %s [%s]\n", get_width(tmp_reg, width),
                   width_specifier(width), reg_names_64[tmp_reg]);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = tmp_reg;
    loc->reg.width = state->slots[i.value].size;
    break;
  case IR_EQ:
    tmp_reg = ensure_reg(state, i.binop.lhs);
    if (tmp_reg < 0)
      return tmp_reg;
    tmp_reg_2 = ensure_reg(state, i.binop.rhs);
    if (tmp_reg_2 < 0)
      return tmp_reg_2;

    width = state->slots[i.binop.lhs].size;

    ret = asprintf(&buf, "\tcmp %s, %s\n\tsete %s\n", get_width(tmp_reg, width),
                   get_width(tmp_reg_2, width), get_width(tmp_reg, 1));
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = tmp_reg;
    loc->reg.width = 1;
    free_reg(state, tmp_reg_2);
    break;
  case IR_NE:
    tmp_reg = ensure_reg(state, i.binop.lhs);
    if (tmp_reg < 0)
      return tmp_reg;
    tmp_reg_2 = ensure_reg(state, i.binop.rhs);
    if (tmp_reg_2 < 0)
      return tmp_reg_2;

    width = state->slots[i.binop.lhs].size;

    ret =
        asprintf(&buf, "\tcmp %s, %s\n\tsetne %s\n", get_width(tmp_reg, width),
                 get_width(tmp_reg_2, width), get_width(tmp_reg, 1));
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = tmp_reg;
    loc->reg.width = 1;
    free_reg(state, tmp_reg_2);
    break;
  case IR_LT:
    tmp_reg = ensure_reg(state, i.binop.lhs);
    if (tmp_reg < 0)
      return tmp_reg;
    tmp_reg_2 = ensure_reg(state, i.binop.rhs);
    if (tmp_reg_2 < 0)
      return tmp_reg_2;

    width = state->slots[i.binop.lhs].size;

    ret = asprintf(&buf, "\tcmp %s, %s\n\tsetl %s\n", get_width(tmp_reg, width),
                   get_width(tmp_reg_2, width), get_width(tmp_reg, 1));
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = tmp_reg;
    loc->reg.width = 1;
    free_reg(state, tmp_reg_2);
    break;
  case IR_GT:
    tmp_reg = ensure_reg(state, i.binop.lhs);
    if (tmp_reg < 0)
      return tmp_reg;
    tmp_reg_2 = ensure_reg(state, i.binop.rhs);
    if (tmp_reg_2 < 0)
      return tmp_reg_2;

    width = state->slots[i.binop.lhs].size;

    ret = asprintf(&buf, "\tcmp %s, %s\n\tsetg %s\n", get_width(tmp_reg, width),
                   get_width(tmp_reg_2, width), get_width(tmp_reg, 1));
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = tmp_reg;
    loc->reg.width = 1;
    free_reg(state, tmp_reg_2);
    break;
  case IR_LE:
    tmp_reg = ensure_reg(state, i.binop.lhs);
    if (tmp_reg < 0)
      return tmp_reg;
    tmp_reg_2 = ensure_reg(state, i.binop.rhs);
    if (tmp_reg_2 < 0)
      return tmp_reg_2;

    width = state->slots[i.binop.lhs].size;

    ret =
        asprintf(&buf, "\tcmp %s, %s\n\tsetle %s\n", get_width(tmp_reg, width),
                 get_width(tmp_reg_2, width), get_width(tmp_reg, 1));
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = tmp_reg;
    loc->reg.width = 1;
    free_reg(state, tmp_reg_2);
    break;
  case IR_GE:
    tmp_reg = ensure_reg(state, i.binop.lhs);
    if (tmp_reg < 0)
      return tmp_reg;
    tmp_reg_2 = ensure_reg(state, i.binop.rhs);
    if (tmp_reg_2 < 0)
      return tmp_reg_2;

    width = state->slots[i.binop.lhs].size;

    ret =
        asprintf(&buf, "\tcmp %s, %s\n\tsetge %s\n", get_width(tmp_reg, width),
                 get_width(tmp_reg_2, width), get_width(tmp_reg, 1));
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_REG;
    loc->reg.id = tmp_reg;
    loc->reg.width = 1;
    free_reg(state, tmp_reg_2);
    break;
  case IR_BRANCH:
    tmp_reg = ensure_reg(state, i.branch.cond);
    if (tmp_reg < 0)
      return tmp_reg;

    ret =
        asprintf(&buf, "\tjz %s\n\tjmp %s\n", i.branch.lfalse, i.branch.ltrue);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_NONE;
    free_reg(state, tmp_reg);
    break;
  case IR_JMP:
    ret = asprintf(&buf, "\tjmp %s\n", i.label);
    if (ret < 0)
      return ret;
    ret = append(state, buf);
    free(buf);
    if (ret < 0)
      return ret;
    loc = &state->value_loc[i.dst];
    loc->kind = LOC_NONE;
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
      ret = dead_branch_elim(f);
      if (ret < 0) {
        free(state.generated);
        return ret;
      }
    }

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
