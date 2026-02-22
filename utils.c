#include "utils.h"
#include "stdlib.h"
#include "string.h"

int sb_init(string_builder *builder) {
  builder->len = 1;
  builder->capacity = 128;
  builder->string = malloc(sizeof(char) * 128);
  if (builder->string == NULL)
    return -1;
  return 0;
}

void sb_free(string_builder *builder) { free(builder->string); }

int sb_append(string_builder *builder, char *to_append) {
  int len = strlen(to_append);
  if (builder->len + len > builder->capacity) {
    builder->capacity *= 2;
    char *new = realloc(builder->string, sizeof(char) * (builder->capacity));
    if (new == NULL) {
      free(builder->string);
      return -1;
    }
    builder->string = new;
  }
  memcpy(builder->string + builder->len - 1, to_append, len);
  builder->len += len;
  return len;
}

int sb_append_free(string_builder *builder, char *to_append) {
  int ret = sb_append(builder, to_append);
  free(to_append);
  return ret;
}

char *sb_build(string_builder *builder) {
  if (builder->len < builder->capacity) {
    char *tmp = realloc(builder->string, sizeof(char) * (builder->len));
    if (tmp == NULL) { // Shouldn't happen, but I guess it could
      free(builder->string);
      return NULL;
    }
    builder->string = tmp;
  }
  builder->string[builder->len - 1] = 0;
  return builder->string;
}
