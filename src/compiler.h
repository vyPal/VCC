#ifndef COMPILER_H
#define COMPILER_H

#include "ir.h"
#include "parser.h"

/*
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
*/

typedef struct {
  int is_alloca;
  int loads;
  int stores;
} mem_info;

typedef struct {
  int offset;
  int size;
} stack_slot;

typedef enum { LOC_NONE, LOC_REG, LOC_STACK, LOC_IMM } location_kind;

typedef struct {
  location_kind kind;

  union {
    int reg;
    int stack;
    long immediate;
  };
} location;

enum {
  REG_RAX,
  REG_RBX,
  REG_RCX,
  REG_RDX,
  REG_RSI,
  REG_RDI,
  REG_R8,
  REG_R9,
  REG_COUNT
};

typedef struct {
  char *generated;
  int output_len;
  int valc;
  stack_slot *slots;
  location *value_loc;
  int reg_used[REG_COUNT];
  module *mod;
  // variable_map *varmap;
  // int varmapc;
} compiler_state;

int generate_asm(module *mod, char **output);

#endif // !COMPILER_H
