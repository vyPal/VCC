#ifndef COMPILER_H
#define COMPILER_H

#include "parser.h"

int generate_asm(ast_node **source, int nodec, char **output);

#endif // !COMPILER_H
