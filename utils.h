#ifndef UTILS_H
#define UTILS_H

typedef struct {
  char *string;
  int len;
  int capacity;
} string_builder;

int sb_init(string_builder *builder);
void sb_free(string_builder *builder);

int sb_append(string_builder *builder, char *to_append);
int sb_append_free(string_builder *builder, char *to_append);

char *sb_build(string_builder *builder);

#endif // !UTILS_H
