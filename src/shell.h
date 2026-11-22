#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>

// Tokenize one input line, return 1 on success and store the token count.
int shell_tokenize(char *line, char **tokens,
                   size_t capacity, size_t *token_count);

#endif

