#include "compiler.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *VERSION = "0.0.1";

typedef struct {
  int input_count;
  char **inputs;
  char *output;
  struct {
    unsigned int help : 1;
    unsigned int print_ast : 1;
  } flags;
} args;

/*
 * Parse arguments from argc and argv into args struct
 *
 * A return code smaller than 0 means that an error has occured.
 * If an error has occured, all memory allocated by this method has been freed.
 * If no error occured, the caller is responsible for freeing the `inputs`
 * string array.
 */
int parse_args(args *args, int argc, char **argv) {
  int index = 0;
  while (index < argc) {
    if (strncmp("-o", argv[index], 2) == 0) {
      if (++index >= argc) {
        printf("Output file name expected after '-o' flag\n");
        return -1;
      }
      args->output = argv[index];
    } else if (strncmp("-h", argv[index], 2) == 0) {
      args->flags.help = 1;
    } else if (strncmp("-a", argv[index], 2) == 0) {
      args->flags.print_ast = 1;
    } else {
      if (args->inputs == NULL) {
        args->inputs = malloc(sizeof(char *) * ++args->input_count);
        if (args->inputs == NULL) {
          printf("Failed to allocate space for input array\n");
          return -1;
        }
      } else {
        char **tmp =
            realloc(args->inputs, sizeof(char *) * ++args->input_count);
        if (tmp == NULL) {
          printf("Failed to realloceate space for input array\n");
          free(args->inputs);
          return -1;
        }
        args->inputs = tmp;
      }
      args->inputs[args->input_count - 1] = argv[index];
    }

    index++;
  }
  return 0;
}

// Initializes uninitialized values in args struct
void set_defaults(args *args) {
  if (args->output == NULL) {
    args->output = "out.s";
  }
}

void print_help(char *cmd) {
  printf("VCC v%s - vyPal's C Compiler\n\n", VERSION);
  printf("A basic compiler for a small subset of the C language, written by "
         "Jakub Palacký (vyPal)\n\n");
  printf("Usage: %s (options) <source files...>\n\n", cmd);
  printf("Available options:\n");
  printf("\t-h\t\tShows this help menu\n");
  printf("\t-o <file>\tSpecifies the output file name (default: 'out.s')\n");
  printf("\t-a\t\tPrint AST after parsing\n");
}

int main(int argc, char **argv) {
  char *cmd = argv[0];
  args args = {0};
  int ret = parse_args(&args, --argc, ++argv);
  if (ret < 0) {
    return ret;
  }
  set_defaults(&args);

  if (args.flags.help) {
    print_help(cmd);
    return 0;
  }

  if (args.input_count == 0) {
    printf("Must supply at least one input file\n");
    return 1;
  } else if (args.input_count > 1) {
    printf("This compiler currently doesn't have support for more than one "
           "input file\n");
    return 1;
  }

  FILE *input = fopen(args.inputs[0], "r");
  if (input == NULL) {
    printf("Failed to open input file\n");
    free(args.inputs);
    return 1;
  }

  fseek(input, 0, SEEK_END);
  long length = ftell(input);
  fseek(input, 0, SEEK_SET);
  char *contents = malloc(sizeof(char) * length + 1);
  if (contents == NULL) {
    printf("Failed to allocate memory for file contents\n");
    free(args.inputs);
    return 1;
  }
  fread(contents, 1, length, input);
  contents[length - 1] = 0;
  fclose(input);

  ast_node **nodes = NULL;
  int nodec = parse_text(contents, &nodes);
  if (args.flags.print_ast)
    for (int i = 0; i < nodec; i++)
      traverse_tree(nodes[i], 0);

  char *out_asm;
  int len = generate_asm(nodes, nodec, &out_asm);
  if (len < 0) {
    printf("Assembly generation failed\n");
    for (int i = 0; i < nodec; i++) {
      free_node(nodes[i]);
    }
    free(nodes);
    free(args.inputs);
    free(contents);
    return 1;
  }

  FILE *output = fopen(args.output, "w");
  if (output == NULL) {
    printf("Failed to open output file\n");
    for (int i = 0; i < nodec; i++) {
      free_node(nodes[i]);
    }
    free(nodes);
    free(args.inputs);
    free(contents);
    return 1;
  }

  fwrite(out_asm, 1, len, output);
  fclose(output);

  free(out_asm);

  for (int i = 0; i < nodec; i++) {
    free_node(nodes[i]);
  }
  free(nodes);
  free(args.inputs);
  free(contents);

  return 0;
}
