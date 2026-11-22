#include <stdlib.h>
#include "filesystem.h"
#include <string.h>

// Find a named child in a directory's entry array.
Node *filesystem_find_entry(Directory *dir, const char *name)
{
    if (dir == NULL || name == NULL)
        return NULL;

    for (size_t i = 0; i < dir->entry_count; i++) {
        if (strcmp(dir->entries[i]->name, name) == 0)
            return dir->entries[i];
    }

    return NULL;
}

// Grow the entry array geometrically.
int filesystem_ensure_entry_capacity(Directory *dir)
{
    if (dir == NULL)
        return 0;

    if (dir->entry_count < dir->entry_capacity)
        return 1;

    // The checks prevent capacity and byte-size overflow.
    size_t new_capacity;
    if (dir->entry_capacity == 0) {
        new_capacity = 4;
    } else {
        if (dir->entry_capacity > (size_t)-1 / 2)
            return 0;
        new_capacity = dir->entry_capacity * 2;
    }

    if (new_capacity > (size_t)-1 / sizeof *dir->entries)
        return 0;

    // Keep the old array valid if realloc fails by assigning through a temporary pointer.
    Node **new_entries = realloc(
        dir->entries,
        new_capacity * sizeof *dir->entries
    );

    if (new_entries == NULL)
        return 0;

    dir->entries = new_entries;
    dir->entry_capacity = new_capacity;
    return 1;
}

// Recursively release a directory tree and free shared inodes after their last link.
static void destroy_directory(Directory *dir)
{
    if (dir == NULL)
        return;

    for (size_t i = 0; i < dir->entry_count; i++) {
        Node *entry = dir->entries[i];

        if (entry->type == NODE_DIR) {
            destroy_directory(entry->data.dir_ref);
        } else if (entry->type == NODE_SYMLINK) {
            free(entry->data.target_path);
        } else if (entry->type == NODE_FILE && entry->data.inode != NULL) {
            Inode *inode = entry->data.inode;

            // Each hard-link node owns one ref_count, shared content is freed only at zero.
            if (inode->ref_count > 0)
                inode->ref_count--;

            if (inode->ref_count == 0) {
                free(inode->content);
                free(inode);
            }
        }

        free(entry->name);
        free(entry);
    }

    free(dir->entries);
    free(dir);
}

// Append a node to a directory and record its parent relationship.
int filesystem_add_entry(Directory *dir, Node *entry)
{
    if (dir == NULL || entry == NULL)
        return 0;

    if (!filesystem_ensure_entry_capacity(dir))
        return 0;

    dir->entries[dir->entry_count] = entry;
    dir->entry_count++;
    entry->parent = dir;

    return 1;
}

// Allocate an empty directory node after validating its name uniqueness.
int filesystem_create_directory(Directory *parent, const char *name)
{
    if (parent == NULL || name == NULL || name[0] == '\0')
        return 0;

    if (strchr(name, '/') != NULL ||
        strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0)
        return 0;

    if (filesystem_find_entry(parent, name) != NULL)
        return 0;

    // Build the complete node first, then publish it with add_entry.
    size_t name_length = strlen(name);
    char *name_copy = malloc(name_length + 1);
    if (name_copy == NULL)
        return 0;
    memcpy(name_copy, name, name_length + 1);

    Node *entry = malloc(sizeof *entry);
    if (entry == NULL) {
        free(name_copy);
        return 0;
    }

    Directory *new_directory = malloc(sizeof *new_directory);
    if (new_directory == NULL) {
        free(entry);
        free(name_copy);
        return 0;
    }

    new_directory->parent = parent;
    new_directory->entries = NULL;
    new_directory->entry_count = 0;
    new_directory->entry_capacity = 0;

    entry->name = name_copy;
    entry->type = NODE_DIR;
    entry->parent = parent;
    entry->data.dir_ref = new_directory;

    // If insertion fails, make free every allocation so the directory remains unchanged.
    if (!filesystem_add_entry(parent, entry)) {
        free(new_directory);
        free(entry->name);
        free(entry);
        return 0;
    }

    return 1;
}

// Split a directory path into its parent path and final name, then create it.
int filesystem_create_directory_path(const ShellState *state, const char *path) {

    if (state == NULL || state->root == NULL || state->current == NULL ||
        path == NULL || path[0] == '\0')
        return 0;

    // Work on a copy because while splitting the final component replaces / with \0.
    size_t path_length = strlen(path);
    char *path_copy = malloc(path_length + 1);
    if (path_copy == NULL)
        return 0;

    memcpy(path_copy, path, path_length + 1);

    char *slash = strrchr(path_copy, '/');
    char *name;
    Directory *parent;

    // A name without / is relative to current; otherwise resolve its parent path.
    if (slash == NULL) {
        name = path_copy;
        parent = state->current;
    } 
    else {
        name = slash + 1;

        if (name[0] == '\0') {
            free(path_copy);
            return 0;
        }

        if (slash == path_copy) {
            parent = state->root;
        } else {
            *slash = '\0';
            parent = filesystem_resolve_directory(state, path_copy);

            if (parent == NULL) {
                free(path_copy);
                return 0;
            }
        }
    }

    int created = filesystem_create_directory(parent, name);
    free(path_copy);
    return created;
}

// Create a regular file with a new inode.
int filesystem_create_file(Directory *parent, const char *name) {

    if (parent == NULL || name == NULL || name[0] == '\0')
        return 0;

    if (strchr(name, '/') != NULL || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;

    if (filesystem_find_entry(parent, name) != NULL)
        return 0;

    // A file node owns its name, while its inode separately owns file bytes and link count.
    size_t name_length = strlen(name);
    char *name_copy = malloc(name_length + 1);
    if (name_copy == NULL)
        return 0;

    memcpy(name_copy, name, name_length + 1);
    
    Node *entry = malloc(sizeof *entry);
    if (entry == NULL) {
        free(name_copy);
        return 0;
    }

    Inode *inode = malloc(sizeof *inode);
    if (inode == NULL) {
        free(entry);
        free(name_copy);
        return 0;
    }

    inode->content = NULL;
    inode->size = 0;
    inode->ref_count = 1;

    entry->name = name_copy;
    entry->type = NODE_FILE;
    entry->parent = parent;
    entry->data.inode = inode;

    if (!filesystem_add_entry(parent, entry)) {
        free(inode);
        free(entry->name);
        free(entry);
        return 0;
    }

    return 1;
}

// Replace or append file.
int filesystem_write_file(Node *file, const char *content, size_t content_size, int append) {
    
    if (file == NULL || file->type != NODE_FILE || file->data.inode == NULL || (content == NULL && content_size > 0) || (append != 0 && append != 1))
        return 0;

    Inode *inode = file->data.inode;

    // Overwrite starts at byte zero; append copies the current bytes before new content.
    size_t old_size = append ? inode->size : 0;

    if (old_size == (size_t)-1 || content_size > (size_t)-1 - old_size - 1)
        return 0;

    size_t new_size = old_size + content_size;

     char *new_content = malloc(new_size + 1);
    if (new_content == NULL)
        return 0;

    if (old_size > 0)
        memcpy(new_content, inode->content, old_size);

    if (content_size > 0)
        memcpy(new_content + old_size, content, content_size);

    new_content[new_size] = '\0';

    free(inode->content);
    inode->content = new_content;
    inode->size = new_size;

    return 1;
}

// Walk directory components and follow symbolic links, with a bounded link depth.
static Directory *filesystem_resolve_directory_depth(
    const ShellState *state,
    const char *path,
    size_t link_depth
) {
    
    if (state == NULL || path == NULL || path[0] == '\0' || link_depth > 16)
        return NULL;

    Directory *current =
        path[0] == '/' ? state->root : state->current;

    if (current == NULL)
        return NULL;

    size_t path_length = strlen(path);
    char *path_copy = malloc(path_length + 1);
    if (path_copy == NULL)
        return NULL;

    memcpy(path_copy, path, path_length + 1);

    // Resolve components individually so dot segments retain their path semantics.
    char *cursor = path_copy;
    while (*cursor != '\0') {
        while (*cursor == '/')
            cursor++;

        if (*cursor == '\0')
            break;

        char *component = cursor;
        while (*cursor != '\0' && *cursor != '/')
            cursor++;

        char *suffix = cursor;
        if (*cursor == '/') {
            *cursor = '\0';
            suffix++;
        }

        if (strcmp(component, ".") == 0) {
            
        } 
        else if (strcmp(component, "..") == 0) {
            if (current->parent != NULL)
                current = current->parent;
        } 
        else {
            Node *entry = filesystem_find_entry(current, component);
            if (entry == NULL) {
                free(path_copy);
                return NULL;
            }

            if (entry->type == NODE_SYMLINK) {
                const char *target_path = entry->data.target_path;
                if (target_path == NULL || target_path[0] == '\0') {
                    free(path_copy);
                    return NULL;
                }

                // Resolve relative targets from the link's parent, then continue with the remaining path.
                ShellState target_state = *state;
                target_state.current = current;

                size_t target_length = strlen(target_path);
                size_t suffix_length = strlen(suffix);
                size_t separator_length = suffix_length > 0 ? 1 : 0;
                size_t max_length = (size_t)-1;
                if (target_length > max_length - separator_length - 1 ||
                    suffix_length >
                        max_length - target_length - separator_length - 1) {
                    free(path_copy);
                    return NULL;
                }

                size_t expanded_length =
                    target_length + separator_length + suffix_length;
                char *expanded_path = malloc(expanded_length + 1);
                if (expanded_path == NULL) {
                    free(path_copy);
                    return NULL;
                }

                memcpy(expanded_path, target_path, target_length);
                if (separator_length > 0)
                    expanded_path[target_length] = '/';
                memcpy(
                    expanded_path + target_length + separator_length,
                    suffix,
                    suffix_length + 1
                );

                Directory *resolved = filesystem_resolve_directory_depth(
                    &target_state,
                    expanded_path,
                    link_depth + 1
                );
                free(expanded_path);
                free(path_copy);
                return resolved;
            }

            if (entry->type != NODE_DIR || entry->data.dir_ref == NULL) {
                free(path_copy);
                return NULL;
            }

            current = entry->data.dir_ref;
        }

        cursor = suffix;
    }

    free(path_copy);
    return current;
}

// Start directory lookup with no symbolic links followed yet.
Directory *filesystem_resolve_directory(const ShellState *state, const char *path) {
    return filesystem_resolve_directory_depth(state, path, 0);
}

// Resolve a file path and follow symbolic links, stopping after a bounded number of links.
static Node *filesystem_resolve_file_depth(const ShellState *state, const char *path, size_t link_depth) {
    
    if (state == NULL || path == NULL || path[0] == '\0' || link_depth > 16)
        return NULL;

    // A file path is split into its containing directory and final name, resolve only
    // the parent as a directory, then inspect the final node's type below.
    size_t path_length = strlen(path);
    char *path_copy = malloc(path_length + 1);
    if (path_copy == NULL)
        return NULL;

    memcpy(path_copy, path, path_length + 1);

    char *slash = strrchr(path_copy, '/');
    char *name;
    Directory *parent;

    if (slash == NULL) {
        name = path_copy;
        parent = state->current;
    } 
    else {
        name = slash + 1;

        if (name[0] == '\0') {
            free(path_copy);
            return NULL;
        }

        if (slash == path_copy) {
            parent = state->root;
        } else {
            *slash = '\0';
            parent = filesystem_resolve_directory(state, path_copy);

            if (parent == NULL) {
                free(path_copy);
                return NULL;
            }
        }
    }
    

    Node *entry = filesystem_find_entry(parent, name);

    if (entry == NULL) {
    free(path_copy);
    return NULL;
    }

    if (entry->type == NODE_SYMLINK) {
        const char *target_path = entry->data.target_path;

        if (target_path == NULL) {
            free(path_copy);
            return NULL;
        }

        // Rebase relative targets at the link's parent without changing the caller's state.
        ShellState target_state = *state;
        target_state.current = parent;

        free(path_copy);
        return filesystem_resolve_file_depth(&target_state, target_path, link_depth + 1);
    }

    if (entry->type != NODE_FILE || entry->data.inode == NULL) {
        free(path_copy);
        return NULL;
    }

    free(path_copy);
    return entry;
}

// Public file lookup starts symbolic-link traversal.
Node *filesystem_resolve_file(const ShellState *state, const char *path) {
    
    return filesystem_resolve_file_depth(state, path, 0);
}
    
// Return an existing file or create a new file at the requested path.
Node *filesystem_get_or_create_file(const ShellState *state, const char *path) {
    
    if (state == NULL || state->root == NULL || state->current == NULL || path == NULL || path[0] == '\0')
        return NULL;

    // Reuse a resolvable file (including a symbolic-link target) instead of creating a
    // duplicate name.
    // Only an unresolved final path proceeds to the creation path below.
    Node *file = filesystem_resolve_file(state, path);
    if (file != NULL)
        return file;

    size_t path_length = strlen(path);
    char *path_copy = malloc(path_length + 1);
    if (path_copy == NULL)
        return NULL;

    memcpy(path_copy, path, path_length + 1);

    // Creation needs an existing parent directory and one valid final component.
    char *slash = strrchr(path_copy, '/');
    char *name;
    Directory *parent;

    if (slash == NULL) {
        name = path_copy;
        parent = state->current;
    } 
    else {
        name = slash + 1;

        if (name[0] == '\0') {
            free(path_copy);
            return NULL;
        }

        if (slash == path_copy) {
            parent = state->root;
        } 
        else {
            *slash = '\0';
            parent = filesystem_resolve_directory(state, path_copy);

            if (parent == NULL) {
                free(path_copy);
                return NULL;
            }
        }
    }

    if (!filesystem_create_file(parent, name)) {
        free(path_copy);
        return NULL;
    }

    file = filesystem_find_entry(parent, name);
    free(path_copy);
    return file;
}

// Remove a file, link, or empty directory and release its owned resources.
int filesystem_remove_node(const ShellState *state, const char *path) {

    if (state == NULL || state->root == NULL || state->current == NULL || path == NULL || path[0] == '\0')
        return 0;

    size_t path_length = strlen(path);
    char *path_copy = malloc(path_length + 1);
    if (path_copy == NULL)
        return 0;

    // Keep the caller's path intact while separating its parent and final name.
    memcpy(path_copy, path, path_length + 1);
        
    char *slash = strrchr(path_copy, '/');
    char *name;
    Directory *parent;

    if (slash == NULL) {
        name = path_copy;
        parent = state->current;
    } 
    else {
        name = slash + 1;

        if (name[0] == '\0') {
            free(path_copy);
            return 0;
        }

        if (slash == path_copy) {
            parent = state->root;
        } 
        else {
            *slash = '\0';
            parent = filesystem_resolve_directory(state, path_copy);

            if (parent == NULL) {
                free(path_copy);
                return 0;
            }
        }
    }
    
        
    Node *entry = filesystem_find_entry(parent, name);

    // Only empty directories can be removed, and the active working directory is protected.
    if (entry == NULL ||
    (entry->type == NODE_DIR &&
     (entry->data.dir_ref->entry_count != 0 ||
      entry->data.dir_ref == state->current))) {
        free(path_copy);
        return 0;
    }

    size_t index = 0;
    while (index < parent->entry_count && parent->entries[index] != entry)
        index++;
    

    if (index == parent->entry_count) {
        free(path_copy);
        return 0;
    }

    // Redesign the parent's array so it contains no hole after removing this node.
    for (size_t i = index; i + 1 < parent->entry_count; i++)
        parent->entries[i] = parent->entries[i + 1];

    parent->entry_count--;

    if (entry->type == NODE_SYMLINK)
        free(entry->data.target_path);
     
    else if (entry->type == NODE_FILE && entry->data.inode != NULL) {
        Inode *inode = entry->data.inode;

        if (inode->ref_count > 0)
            inode->ref_count--;

        if (inode->ref_count == 0) {
            free(inode->content);
            free(inode);
        }
    }

    else if (entry->type == NODE_DIR)
        destroy_directory(entry->data.dir_ref);

    free(entry->name);
    free(entry);
    free(path_copy);
    return 1;

}

// Find the final entry of a path without following it if it is a symbolic link.
static Node *find_final_entry(const ShellState *state, const char *path) {

    if (state == NULL || path == NULL || path[0] == '\0')
        return NULL;

    size_t path_length = strlen(path);
    char *path_copy = malloc(path_length + 1);
    if (path_copy == NULL)
        return NULL;

    memcpy(path_copy, path, path_length + 1);

    char *slash = strrchr(path_copy, '/');
    char *name;
    Directory *parent;

    if (slash == NULL) {
        name = path_copy;
        parent = state->current;
    } 
    else {
        name = slash + 1;

        if (slash == path_copy) {
            parent = state->root;
        } else {
            *slash = '\0';
            parent = filesystem_resolve_directory(state, path_copy);
        }
    }

    Node *entry = NULL;
    if (parent != NULL && name[0] != '\0')
        entry = filesystem_find_entry(parent, name);

    free(path_copy);
    return entry;
}

// Add another file node(hard_link) that points to the same inode as the source.
int filesystem_create_hard_link(const ShellState *state, const char *source_path, const char *link_path) {
    
    if (state == NULL || state->root == NULL || state->current == NULL || source_path == NULL || source_path[0] == '\0' || link_path == NULL || link_path[0] == '\0')
        return 0;

    // Hard links are allowed only to regular files; the source is not followed,
    // so directories and symbolic links are rejected.
    Node *source = find_final_entry(state, source_path);
    if (source == NULL || source->type != NODE_FILE || source->data.inode == NULL)
        return 0;

    size_t link_path_length = strlen(link_path);
    char *path_copy = malloc(link_path_length + 1);
    if (path_copy == NULL)
        return 0;

    memcpy(path_copy, link_path, link_path_length + 1);

    // Split the new path exactly as for file creation: its parent must exist already.
    char *slash = strrchr(path_copy, '/');
    char *link_name;
    Directory *parent;

    if (slash == NULL) {
        link_name = path_copy;
        parent = state->current;
    } 
    else {
        link_name = slash + 1;

        if (link_name[0] == '\0') {
            free(path_copy);
            return 0;
        }

        if (slash == path_copy) {
            parent = state->root;
        } else {
            *slash = '\0';
            parent = filesystem_resolve_directory(state, path_copy);

            if (parent == NULL) {
                free(path_copy);
                return 0;
            }
        }
    }

    if (parent == NULL || link_name[0] == '\0' ||
        strcmp(link_name, ".") == 0 ||
        strcmp(link_name, "..") == 0 ||
        filesystem_find_entry(parent, link_name) != NULL) {
        free(path_copy);
        return 0;
    }

    size_t name_length = strlen(link_name);
    char *name_copy = malloc(name_length + 1);
    if (name_copy == NULL) {
        free(path_copy);
        return 0;
    }

    memcpy(name_copy, link_name, name_length + 1);
     
    Node *link = malloc(sizeof *link);
    if (link == NULL) {
        free(name_copy);
        free(path_copy);
        return 0;
    }

    // The new directory entry has its own name but points at the source's shared inode.
    link->name = name_copy;
    link->type = NODE_FILE;
    link->parent = parent;
    link->data.inode = source->data.inode;

    Inode *inode = source->data.inode;

    if (inode->ref_count == (size_t)-1) {
        free(link->name);
        free(link);
        free(path_copy);
        return 0;
    }

    // Increment only after insertion succeeds.
    if (!filesystem_add_entry(parent, link)) {
        free(link->name);
        free(link);
        free(path_copy);
        return 0;
    }

    inode->ref_count++;
    free(path_copy);
    return 1;
}

// Store a separate target path; the target must resolve when the link is created.
int filesystem_create_symlink(const ShellState *state, const char *target_path, const char *link_path) {
    
    if (state == NULL || state->root == NULL ||
        state->current == NULL ||
        target_path == NULL || target_path[0] == '\0' ||
        link_path == NULL || link_path[0] == '\0')
        return 0;

    

    // Resolve the new link's parent first; this parent is also the base for relative targets.
    size_t link_path_length = strlen(link_path);
    char *path_copy = malloc(link_path_length + 1);
    if (path_copy == NULL)
        return 0;

    memcpy(path_copy, link_path, link_path_length + 1);

    char *slash = strrchr(path_copy, '/');
    char *link_name;
    Directory *parent;

    if (slash == NULL) {
        link_name = path_copy;
        parent = state->current;
    } 
    else {
        link_name = slash + 1;

        if (link_name[0] == '\0') {
            free(path_copy);
            return 0;
        }

        if (slash == path_copy) {
            parent = state->root;
        } 
        else {
            *slash = '\0';
            parent = filesystem_resolve_directory(state, path_copy);

            if (parent == NULL) {
                free(path_copy);
                return 0;
            }
        }
    }

    if (parent == NULL || link_name[0] == '\0' ||
        strcmp(link_name, ".") == 0 ||
        strcmp(link_name, "..") == 0 ||
        filesystem_find_entry(parent, link_name) != NULL) {
        
        free(path_copy);
        return 0;
    }

    // Validate the target relative to the link location, while leaving the real shell state
    // untouched; the target must currently be a regular file, not a directory or another link.
    ShellState target_state = *state;
    target_state.current = parent;

    Node *target_entry = find_final_entry(&target_state, target_path);
    if (target_entry == NULL || target_entry->type != NODE_FILE) {
        free(path_copy);
        return 0;
    }
    
    size_t name_length = strlen(link_name);
    char *name_copy = malloc(name_length + 1);
    if (name_copy == NULL) {
        
        free(path_copy);
        return 0;
    }
    
    memcpy(name_copy, link_name, name_length + 1);

    size_t target_length = strlen(target_path);
    char *target_copy = malloc(target_length + 1);
    
    if (target_copy == NULL) {
        
        free(name_copy);
        free(path_copy);
        return 0;
    }
    
    memcpy(target_copy, target_path, target_length + 1);

    Node *link = malloc(sizeof *link);
    
    if (link == NULL) {

        free(target_copy);
        free(name_copy);
        free(path_copy);
        return 0;
    }

    link->name = name_copy;
    link->type = NODE_SYMLINK;
    link->parent = parent;
    link->data.target_path = target_copy;

    if (!filesystem_add_entry(parent, link)) {
        
        free(link->data.target_path);
        free(link->name);
        free(link);
        free(path_copy);
        return 0;
    }

    free(path_copy);
    return 1;

}

// Copy file bytes to the destination, this operation does not create a hard link.
int filesystem_copy_file(const ShellState *state, const char *source_path, const char *destination_path) {
    
    if (state == NULL || state->root == NULL ||
        state->current == NULL ||
        source_path == NULL || source_path[0] == '\0' ||
        destination_path == NULL || destination_path[0] == '\0')
        return 0;

    // Resolve the source before opening the destination so a bad source cannot create a file.
    Node *source = filesystem_resolve_file(state, source_path);
    if (source == NULL)
        return 0;

    Node *destination = filesystem_get_or_create_file(state, destination_path);

    if (destination == NULL)
        return 0;

    // Copy bytes through the destination's inode, no new directory entry aliases the source.
    return filesystem_write_file(destination, source->data.inode->content, source->data.inode->size,0);
}

// Move a node by changing its parent entry and name without copying its contents.
int filesystem_move_node(const ShellState *state, const char *source_path, const char *destination_path) {
    if (state == NULL || state->root == NULL ||
        state->current == NULL ||
        source_path == NULL || source_path[0] == '\0' ||
        destination_path == NULL || destination_path[0] == '\0')
        return 0;
    
    // Resolve the source's containing directory and final name separately, the source itself
    // is looked up without following a final symbolic link so the link node can be moved.
    size_t source_path_length = strlen(source_path);
    char *source_copy = malloc(source_path_length + 1);
    if (source_copy == NULL)
        return 0;

    memcpy(source_copy, source_path, source_path_length + 1);

    char *slash = strrchr(source_copy, '/');
    char *source_name;
    Directory *source_parent;

    if (slash == NULL) {
        source_name = source_copy;
        source_parent = state->current;
    } 
    else {
        source_name = slash + 1;

        if (source_name[0] == '\0') {
            free(source_copy);
            return 0;
        }

        if (slash == source_copy) {
            source_parent = state->root;
        } 
        else {
            *slash = '\0';
            source_parent =filesystem_resolve_directory(state, source_copy);

            if (source_parent == NULL) {
                free(source_copy);
                return 0;
            }
        }
    }

    Node *source = filesystem_find_entry(source_parent, source_name);
    if (source == NULL) {
        free(source_copy);
        return 0;
    }

    // The destination is a new path, not an existing directory to move into, reject collisions.
    size_t destination_path_length = strlen(destination_path);
    char *destination_copy = malloc(destination_path_length + 1);
    if (destination_copy == NULL) {
        free(source_copy);
        return 0;
    }

    memcpy(destination_copy, destination_path, destination_path_length + 1);

    slash = strrchr(destination_copy, '/');
    char *destination_name;
    Directory *destination_parent;

    if (slash == NULL) {
        destination_name = destination_copy;
        destination_parent = state->current;
    } else {
        destination_name = slash + 1;

        if (destination_name[0] == '\0') {
            free(destination_copy);
            free(source_copy);
            return 0;
        }

        if (slash == destination_copy) {
            destination_parent = state->root;
        } else {
            *slash = '\0';
            destination_parent =
                filesystem_resolve_directory(state, destination_copy);

            if (destination_parent == NULL) {
                free(destination_copy);
                free(source_copy);
                return 0;
            }
        }
    }

    if (destination_parent == NULL || destination_name[0] == '\0' ||
        strcmp(destination_name, ".") == 0 ||
        strcmp(destination_name, "..") == 0 ||
        filesystem_find_entry(destination_parent, destination_name) != NULL) {
        
        free(destination_copy);
        free(source_copy);
        return 0;
    }

    size_t destination_name_length = strlen(destination_name);
    char *new_name = malloc(destination_name_length + 1);
    if (new_name == NULL) {
        free(destination_copy);
        free(source_copy);
        return 0;
    }

    memcpy(new_name, destination_name, destination_name_length + 1);

    if (source->type == NODE_DIR) {
        // A directory cannot be moved into itself or any of its descendants.
        Directory *moving_directory = source->data.dir_ref;

        for (Directory *ancestor = destination_parent;
             ancestor != NULL;
             ancestor = ancestor->parent) {
            if (ancestor == moving_directory) {
                free(new_name);
                free(destination_copy);
                free(source_copy);
                return 0;
            }
        }
    }

    // Reserve destination space before detaching the source, so allocation failure cannot
    // leave the node missing from both directories.
    if (!filesystem_ensure_entry_capacity(destination_parent)) {
        free(new_name);
        free(destination_copy);
        free(source_copy);
        return 0;
    }

    size_t source_index = 0;
    while (source_index < source_parent->entry_count &&
           source_parent->entries[source_index] != source) {
        source_index++;
    }

    if (source_index == source_parent->entry_count) {
        free(new_name);
        free(destination_copy);
        free(source_copy);
        return 0;
    }

    // Once all failure-prone checks are complete, detach and attach the same node pointer.
    for (size_t i = source_index; i + 1 < source_parent->entry_count; i++)
        source_parent->entries[i] = source_parent->entries[i + 1];

    source_parent->entry_count--;

    free(source->name);
    source->name = new_name;
    source->parent = destination_parent;
    destination_parent->entries[destination_parent->entry_count++] = source;

    if (source->type == NODE_DIR)
        source->data.dir_ref->parent = destination_parent;

    free(destination_copy);
    free(source_copy);
    return 1;

}

// Allocate an empty root directory and make it the initial working directory.
int filesystem_init(ShellState *state) {
    // The root has no parent.
    state->root = malloc(sizeof *state->root);
    if (state->root == NULL)
        return 0;
    
    state->root->parent = NULL;
    state->root->entries = NULL;
    state->root->entry_count = 0;
    state->root->entry_capacity = 0;
    state->current = state->root;

    return 1;
}

// Release the root tree and clear both state pointers.
void filesystem_destroy(ShellState *state) {
    if (state == NULL)
        return;

    destroy_directory(state->root);
    state->root = NULL;
    state->current = NULL;
}

// Find the node that owns a child directory within its parent.
static const Node *find_directory_entry(const Directory *parent, const Directory *child) {
    
    if (parent == NULL || child == NULL)
        return NULL;

    // A directory stores its parent pointer, while the parent stores the child's name;
    // search the parent's entries to recover that name for path construction.
    for (size_t i = 0; i < parent->entry_count; i++) {
        const Node *entry = parent->entries[i];

        if (entry->type == NODE_DIR && entry->data.dir_ref == child)
            return entry;
    }

    return NULL;
}

// Construct an allocated absolute path by measuring then filling it from leaf to root.
char *filesystem_get_path(const ShellState *state)
{
    if (state == NULL || state->root == NULL || state->current == NULL)
        return NULL;

    // First walk upward to validate links and calculate the exact allocation size.
    size_t path_length = 0;
    const Directory *dir = state->current;

    while (dir != state->root) {
        if (dir->parent == NULL)
            return NULL;

        const Node *entry = find_directory_entry(dir->parent, dir);
        if (entry == NULL)
            return NULL;

        size_t name_length = strlen(entry->name);
        if (name_length + 1 > (size_t)-1 - path_length)
            return NULL;

        path_length += name_length + 1;
        dir = dir->parent;
    }

    if (path_length == 0)
        path_length = 1;

    if (path_length == (size_t)-1)
        return NULL;

    char *path = malloc(path_length + 1);
    if (path == NULL)
        return NULL;

    // Fill backwards from the leaf because each parent name is discovered while walking up.
    path[path_length] = '\0';
    size_t position = path_length;
    dir = state->current;

    while (dir != state->root) {
        const Node *entry = find_directory_entry(dir->parent, dir);
        size_t name_length = strlen(entry->name);

        position -= name_length;
        memcpy(path + position, entry->name, name_length);

        position--;
        path[position] = '/';

        dir = dir->parent;
    }

    if (state->current == state->root)
        path[0] = '/';

    return path;
      
    
}
