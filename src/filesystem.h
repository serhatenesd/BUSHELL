#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stddef.h>

// Nodes distinguish regular files, directories, and symbolic links.
typedef enum {
    NODE_FILE,
    NODE_DIR,
    NODE_SYMLINK
} NodeType;

// Hard-linked file nodes share this content object and its reference count.
typedef struct Inode {
    char *content;
    size_t size;
    size_t ref_count;
} Inode;

typedef struct Directory Directory;

// A node stores its own name and points to data appropriate for its type.
typedef struct Node {
    char *name;
    NodeType type;
    Directory *parent;
    union {
        Inode *inode;
        char *target_path;
        Directory *dir_ref;
    } data;
} Node;

struct Directory{
    // Parent links support path traversal; entries grow dynamically as needed.
    Directory *parent;
    Node **entries;
    size_t entry_count;
    size_t entry_capacity;
};

// The root is fixed for the session; current tracks the shell's working directory.
typedef struct {
    Directory *root;
    Directory *current;
} ShellState;

// Initialize and later release the complete in-memory filesystem.
int filesystem_init(ShellState *state);

void filesystem_destroy(ShellState *state);

Node *filesystem_find_entry(Directory *dir, const char *name);

int filesystem_ensure_entry_capacity(Directory *dir);

int filesystem_add_entry(Directory *dir, Node *entry);

int filesystem_create_directory(Directory *parent, const char *name);

int filesystem_create_directory_path(const ShellState *state, const char *path);

Directory *filesystem_resolve_directory(const ShellState *state, const char *path);

Node *filesystem_resolve_file(const ShellState *state, const char *path);

Node *filesystem_get_or_create_file(const ShellState *state, const char *path);

char *filesystem_get_path(const ShellState *state);

int filesystem_write_file(Node *file, const char *content, size_t content_size, int append);

int filesystem_create_file(Directory *parent, const char *name);

int filesystem_remove_node(const ShellState *state, const char *path);

int filesystem_create_hard_link(const ShellState *state, const char *source_path, const char *link_path);

int filesystem_copy_file(const ShellState *state, const char *source_path, const char *destination_path);

int filesystem_create_symlink(const ShellState *state, const char *target_path, const char *link_path);

int filesystem_move_node(const ShellState *state, const char *source_path, const char *destination_path);


#endif
