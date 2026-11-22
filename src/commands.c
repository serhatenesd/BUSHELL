#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "commands.h"
#include "shell.h"

#define LINE_BUF_SIZE 1024

// Keep directory listings sorting by name.
static int compare_entries(const void *left, const void *right) {

    const Node *left_node = *(Node * const *)left;
    const Node *right_node = *(Node * const *)right;

    return strcmp(left_node->name, right_node->name);
}

// Print the current directory's tree recursively, pointing directories and links.
static void print_tree(Directory *dir, size_t depth)
{
    if (dir->entry_count > 1) {
        qsort(dir->entries, dir->entry_count, sizeof dir->entries[0], compare_entries);
    }

    for (size_t i = 0; i < dir->entry_count; i++) {
        Node *entry = dir->entries[i];

        for (size_t level = 0; level < depth; level++)
            printf("  ");

        printf("%s", entry->name);

        if (entry->type == NODE_DIR)
            printf("/");
        else if (entry->type == NODE_SYMLINK)
            printf("@");

        printf("\n");

        if (entry->type == NODE_DIR)
            print_tree(entry->data.dir_ref, depth + 1);
    }
}

// Parse a line using the shell tokenizer, decide its arguments, and dispatch it to the relevant operation.
int commands_process_line(char *line, ShellState *state) {
    // Each token points to a word inside line, this array stores where each word starts.
    char *tokens[LINE_BUF_SIZE / 2 + 1];
    size_t token_count;

    if (!shell_tokenize(line, tokens, sizeof tokens / sizeof tokens[0], &token_count)) {
        printf("bushell: error\n");
        return 0;
    }

    if (token_count == 0)
        return 0;

    // Exit is the only command that wants to stop.
    if (token_count == 1 && strcmp(tokens[0], "exit") == 0)
        return 1;
    
    // pwd and tree get their displayed paths from the filesystem_get_path function.
    if (token_count == 1 && strcmp(tokens[0], "pwd") == 0) {
        char *path = filesystem_get_path(state);

        if (path == NULL) {
            printf("bushell: error\n");
        } else {
            printf("%s\n", path);
            free(path);
        }

        return 0;
    }

    if (strcmp(tokens[0], "tree") == 0) {
        if (token_count != 1) {
            printf("bushell: error\n");
            return 0;
        }

        char *path = filesystem_get_path(state);
        if (path == NULL) {
            printf("bushell: error\n");
            return 0;
        }

        printf("%s\n", path);
        free(path);

        // The listing starts at the working directory, print_tree is not listing the above directories.
        print_tree(state->current, 1);
        return 0;
    }

    // cd is changing the working directory.
    if (strcmp(tokens[0], "cd") == 0) {
        if (token_count != 2) {
            printf("bushell: error\n");
            return 0;
        }

        Directory *target =
            filesystem_resolve_directory(state, tokens[1]);

        // Change directory only after the complete path has resolved(filesystem_resolve_directory) successfully.
        if (target == NULL)
            printf("bushell: error\n");
        else
            state->current = target;

        return 0;
    }

    // ls lists the contents of a directory, optionally showing hidden names and resolving a target path.
    if (strcmp(tokens[0], "ls") == 0) {
        int show_all = 0;
        Directory *dir = state->current;

        if (token_count > 3) {
            printf("bushell: error\n");
            return 0;
        }

        if (token_count >= 2) {
            // The first argument can be either -a or a path; the second argument is a path only if -a was first.
            // -a is point to showing hidden names.
            if (strcmp(tokens[1], "-a") == 0) {
                show_all = 1;

                if (token_count == 3) {
                    dir = filesystem_resolve_directory(state, tokens[2]);
                }
            } else if (token_count == 2) {
                dir = filesystem_resolve_directory(state, tokens[1]);
            } else {
                printf("bushell: error\n");
                return 0;
            }

            if (dir == NULL) {
                printf("bushell: error\n");
                return 0;
            }
        }

        if (dir->entry_count > 1) {
            qsort(dir->entries, dir->entry_count,
                sizeof dir->entries[0], compare_entries);
        }

        size_t printed = 0;

        if (show_all) {
            printf("./");
            printed++;
            printf("  ../");
            printed++;
        }

        for (size_t i = 0; i < dir->entry_count; i++) {
            Node *entry = dir->entries[i];

            // Hidden names are filtered by their leading dot unless -a was writed.
            if (!show_all && entry->name[0] == '.')
                continue;

            if (printed > 0)
                printf(" ");

            printf("%s", entry->name);

            if (entry->type == NODE_DIR)
                printf("/");
            else if (entry->type == NODE_SYMLINK)
                printf("@");

            printed++;
        }

        printf("\n");
        return 0;
    }

    // mkdir creates the directory at the given path; its parent directory must already exist.
    if (token_count >= 1 && strcmp(tokens[0], "mkdir") == 0) {
        if (token_count != 2 ||
            !filesystem_create_directory_path(state, tokens[1]))
            
            printf("bushell: error\n");

        return 0;
    }

    // touch creates a file if it does not exist.
    if (strcmp(tokens[0], "touch") == 0) {
        if (token_count != 2 || filesystem_get_or_create_file(state, tokens[1]) == NULL)
            printf("bushell: error\n");

        return 0;
    }
    // cat prints the contents of a file to stdout.
    if (strcmp(tokens[0], "cat") == 0) {
        if (token_count != 2) {
            printf("bushell: error\n");
            return 0;
        }

        Node *file = filesystem_resolve_file(state, tokens[1]);
        if (file == NULL) {
            printf("bushell: error\n");
            return 0;
        }

        // Use the recorded byte count so output does not depend on string termination.
        if (file->data.inode->size > 0)
            fwrite(file->data.inode->content, 1, file->data.inode->size, stdout);
             

        return 0;
    }

    // du prints the size of a file in bytes.
    if (strcmp(tokens[0], "du") == 0) {
        if (token_count != 2) {
            printf("bushell: error\n");
            return 0;
        }

        Node *file = filesystem_resolve_file(state, tokens[1]);
        if (file == NULL) {
            printf("bushell: error\n");
            return 0;
        }

        printf("%zu\t%s\n", file->data.inode->size, tokens[1]);
        return 0;
    }

    // rm removes a file, directory, or link, directories must be empty to remove.
    if (strcmp(tokens[0], "rm") == 0) {
        if (token_count != 2 ||
            !filesystem_remove_node(state, tokens[1]))
            printf("bushell: error\n");

        return 0;
    }

    // ln creates a symbolic link or a hard link, depending on the -s flag.
    if (strcmp(tokens[0], "ln") == 0) {
        
        if (token_count == 4 && strcmp(tokens[1], "-s") == 0) {
            if (!filesystem_create_symlink(state, tokens[2], tokens[3]))
                printf("bushell: error\n");
        } 
        else if (token_count == 3) {
            if (!filesystem_create_hard_link(state, tokens[1], tokens[2]))
                printf("bushell: error\n");
        } 
        else {
            printf("bushell: error\n");
        }

        return 0;
    }

    // cp copies content of a file to another file, creating the target file if it does not exist.
    if (strcmp(tokens[0], "cp") == 0) {
        if (token_count != 3 || !filesystem_copy_file(state, tokens[1], tokens[2]))
            printf("bushell: error\n");

        return 0;
    }

    // Move or rename a node(file, directory, or link) by attaching it to a new path.
    if (strcmp(tokens[0], "mv") == 0) {
        if (token_count != 3 ||
            !filesystem_move_node(state, tokens[1], tokens[2]))
            printf("bushell: error\n");

        return 0;
    }

    // echo prints its arguments to stdout or redirecting them to a file.
    if (token_count >= 1 && strcmp(tokens[0], "echo") == 0) {
        
        size_t redirect_index = token_count;
        int append = 0;

        for (size_t i = 1; i < token_count; i++) {

            // single > is overwrite, double >> is append.
            if (strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], ">>") == 0) {

                if (redirect_index != token_count || i + 2 != token_count) {

                    printf("bushell: error\n");
                    return 0;
                }

                redirect_index = i;
                append = strcmp(tokens[i], ">>") == 0;
            }
        }
        
        if (redirect_index < token_count) {
            // Reconstruct the message because tokenization removed quotes and split on spaces.
            size_t content_size = 1;

            for (size_t i = 1; i < redirect_index; i++) {
                content_size += strlen(tokens[i]);

                if (i + 1 < redirect_index)
                    content_size++;
            }

            char *content = malloc(content_size + 1);
            if (content == NULL) {
                printf("bushell: error\n");
                return 0;
            }

            size_t position = 0;

            for (size_t i = 1; i < redirect_index; i++) {
                if (i > 1)
                    content[position++] = ' ';

                size_t word_length = strlen(tokens[i]);
                memcpy(content + position, tokens[i], word_length);
                position += word_length;
            }

            content[position++] = '\n';
            content[position] = '\0';

            // Resolve or create the destination first; the append flag controls whether
            // filesystem_write_file preserves its existing bytes or replaces them.
            Node *file = filesystem_get_or_create_file(state, tokens[redirect_index + 1]);

            if (file == NULL || !filesystem_write_file(file, content, position, append)) {
                free(content);
                printf("bushell: error\n");
                return 0;
            }

            free(content);
            return 0;
        }

        // Without redirection, echo writes the words directly to standard output.
        for (size_t i = 1; i < token_count; i++) {
            if (i > 1)
                printf(" ");
            printf("%s", tokens[i]);
        }
        printf("\n");
        return 0;


    }

    printf("bushell: error\n");
    return 0;
    

}