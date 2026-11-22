#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "filesystem.h"
#include "commands.h"


#define LINE_BUF_SIZE  1024

// Start the in-memory filesystem and process one command per input line.
int main(void)
{
    char line[LINE_BUF_SIZE];
    ShellState state;
    
    // The shell cannot run unless its initial filesystem state is ready.
    if (!filesystem_init(&state)) {
        printf("bushell: error\n");
        return EXIT_FAILURE;
    }

    while (1) {
        char *current_path = filesystem_get_path(&state);

        // Build the prompt from the current directory and release the allocated path.
        if (current_path == NULL) {
            printf("bushell: error\n");
            filesystem_destroy(&state);
            return EXIT_FAILURE;
        }

        printf("user@boun: %s$ ", current_path);
        free(current_path);
        fflush(stdout);

        // EOF ends the loop.
        if (!fgets(line, sizeof line, stdin))
            break;
        if (commands_process_line(line, &state))
            break;
    }

    // Release every node and directory before leaving the program.
    filesystem_destroy(&state);
    return 0;
    
}
