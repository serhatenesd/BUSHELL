#include <string.h>
#include "shell.h"


int shell_tokenize(char *line, char **tokens, size_t capacity, size_t *token_count) {

    // Reject invalid output pointers.
    if (line == NULL || tokens == NULL || token_count == NULL)
        return 0;
    *token_count = 0;
    line[strcspn(line, "\r\n")] = '\0';

    // Both pointers refer to this input buffer: read scans the input, while write points
    // to the position where the next token character will be stored.
    char *read = line;
    char *write = line;

    while (*read != '\0') {
        while (*read == ' ' || *read == '\t')
            read++;

        if (*read == '\0')
            break;

        if (*token_count >= capacity)
            return 0;

        tokens[(*token_count)++] = write;
        int in_quotes = 0;
        
// Copy each token into the input, omitting quote marks but keeping spaces inside quotes.
        while (*read != '\0') {
            if (*read == '\\'){
                read++;
                if (*read == '\0')
                    return 0;
                *write++ = *read++;
            }
            else if (*read == '"') {
                in_quotes = !in_quotes;
                read++;
            }
            else if (!in_quotes && (*read == ' ' || *read == '\t')) {
                break;
            }
            else 
                *write++ = *read++;
        }

        // A escape used at the final of the line or single quote makes the command line invalid.
        if (in_quotes)
            return 0;

        while (*read == ' ' || *read == '\t')
            read++;
        *write++ = '\0';
    }
    
    return 1;

}
