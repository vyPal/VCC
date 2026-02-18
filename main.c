#include "compiler.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int input_count;
  char **inputs;
  char *output;
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

int main(int argc, char **argv) {
  args args = {0};
  int ret = parse_args(&args, --argc, ++argv);
  if (ret < 0) {
    return ret;
  }
  set_defaults(&args);

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
  printf("Parsed %d nodes\n", nodec);
  for (int i = 0; i < nodec; i++) {
    traverse_tree(nodes[i], 0);
  }

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
