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
        args->inputs = malloc(sizeof(char *));
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

int main(int argc, char **argv) {
  args a;
  int ret = parse_args(&a, argc++, argv++);
  if (ret < 0) {
    return ret;
  }

  ast_node *nodes = NULL;
  ret = parse_text("int main(int a, int b){int test = 7;}", &nodes);
  printf("Parsed %d nodes\n", ret);

  return 0;
}
