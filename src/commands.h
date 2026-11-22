#ifndef COMMANDS_H
#define COMMANDS_H

#include "filesystem.h"

// Execute one parsed command according to its tokens.
int commands_process_line(char *line, ShellState *state);


#endif