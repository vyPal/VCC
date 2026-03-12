#ifndef IR_H
#define IR_H

#include "stdint.h"

// Aliases for IDs which will be used in the IR as pointers
typedef uint32_t value_id;
typedef uint32_t block_id;
typedef uint32_t func_id;

// Currently supported types
typedef enum { TY_I64, TY_I32, TY_VOID, TY_PTR } type_kind;

// Container for additional type data
typedef struct type_def_t {
  type_kind kind;
  struct type_def_t *base; // only if kind == TY_PTR
} type_def;

// Currently supported operatons/instructions
typedef enum {
  IR_ADD,
  IR_SUB,
  IR_MUL,
  IR_SDIV,
  IR_SREM,
  IR_CONST,
  IR_ALLOCA,
  IR_STORE,
  IR_LOAD,
  IR_CALL,
  IR_RET
} opcode;

// Container for instruction
typedef struct {
  opcode op;
  type_def ret;
  value_id dst;

  union {
    struct {
      value_id lhs, rhs; // Or dst, src in this order
    } binop;
    long constant;
    value_id value;
    struct {
      value_id value;
      int present;
    } optional;
    struct {
      func_id func;
      value_id *args;
      int argc;
    } call;
    struct {
      type_def type;
      int count;
    } alloca;
  };
} instruction;

// Container for block
typedef struct {
  char *label;
  block_id id;
  instruction *instructions;
  int instruction_count;
  int instruction_capacity;
} block;

// Container for function
typedef struct {
  char *name;
  func_id id;

  type_def ret_type;

  type_def *arg_types;
  value_id *arg_values;
  int argc;

  block *blocks;
  int block_count;
  int block_capacity;

  int next_value_id;

  // Values used in compilation
  int changed;
} function;

// Container for module
typedef struct {
  function *functions;
  int function_count;
  int function_capacity;
} module;

// Creates a new IR module
module new_module();

/*
 * Creates a new function in module
 *
 * Arguments:
 *  - *module: Module to add function to
 *
 * Returns pointer to new function or in case of an error NULL will be returned
 * and all memory owned by module will be freed
 *
 * Important: pointer is only guaranteed to be valid until the next function is
 * added. Do **not** permanently store this pointer, instead store the function
 * id which is guaranteed not to change
 */
function *new_function(module *m);
// Create a new value id
value_id new_value(function *f);
/*
 * Adds an argument to the function
 *
 * Arguments:
 *  - *function: Function to add argument too
 *  - type_def: Type definition for the argument
 *  - value_id: Value ID under which the argument will be accessible
 *
 * Returns < 0 in case of error and all memory owned by function will be freed
 */
int function_add_argument(function *f, type_def t, value_id id);

/*
 * Creates a new block in function
 *
 * Arguments:
 *  - *function: Function to add block to
 *
 * Returns pointer to new blockk or in case of an error NULL will be returned
 * and all memory owned by function will be freed
 *
 * Important: pointer is only guaranteed to be valid until the next block is
 * added. Do **not** permanently store this pointer, instead store the block id
 * which is guaranteed not to change
 */
block *new_block(function *f);

/*
 * Creates a new instruction inside a function's block
 *
 * Arguments:
 *  - *function: Function that contains the value_id counter
 *  - *block: Block to add instruction to
 *
 * Returns pointer to new instruction or in case of an error NULL will be
 * returned and all memory owned by the function and block will be freed
 *
 * Important: pointer is only guaranteed to be valid until the next instruction
 * is added. Do **not** permanently store this pointer, instead store the value
 * id of this instruction, which is guaranteed not to change
 */
instruction *new_instruction(function *f, block *b);

// Creates a new pointer type
type_def pointer_to(type_def *base);

// Cleans up all memory owned by module
void clean_module(module *mod);

// Clean up all memory owned by type_def
void clean_type(type_def t);

// Prints the text representation of the IR module
void print_text_repr(module *mod);

// Removes an instruction from a block by it's dst
void remove_instuction(function *f, value_id rem);

// Recalculates value_ids
int recalculate_ids(function *f);

#endif // !IR_H
