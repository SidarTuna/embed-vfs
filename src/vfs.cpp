#include "vfs.h"

DataBlock block_pool[MAX_BLOCKS];
VFSNode node_pool[MAX_NODES];
int node_count = 0;

VFSNode* root_dir;
VFSNode* current_dir;

// Centralized path traversal to save ROM/RAM
VFSNode* resolve_path(const char* path) {
    if (path == NULL) return current_dir;
    if (strcmp(path, "/") == 0) return root_dir;

    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    VFSNode* target_dir = current_dir;
    if (path_copy[0] == '/') target_dir = root_dir;
    char *saveptr_path;
    char* token = strtok_r(path_copy, "/", &saveptr_path);
    while (token != NULL) {
        char* next_token = strtok_r(NULL, "/", &saveptr_path); 
        
        if (strcmp(token, ".") == 0) {
            // Stay
        } 
        else if (strcmp(token, "..") == 0) {
            target_dir = target_dir->parent;
        } 
        else {
            VFSNode* child = target_dir->first_child;
            int found = 0;
            while (child != NULL) {
                if (strncmp(child->name, token, MAX_NAME_LEN - 1) == 0) {
                    if (child->is_dir || next_token == NULL) {
                        target_dir = child;
                        found = 1;
                        break;
                    }
                }
                child = child->next_sibling;
            }
            if (!found) return NULL; 
        }
        token = next_token;
    }
    return target_dir;
}

void init_vfs() {
    for (int i = 0; i < MAX_NODES; i++) node_pool[i].is_active = 0;
    for (int i = 0; i < MAX_BLOCKS; i++) block_pool[i].is_active = 0;
    
    root_dir = &node_pool[0];
    root_dir->is_active = 1;
    strcpy(root_dir->name, "/");
    root_dir->is_dir = 1;
    root_dir->start_block = -1;
    root_dir->size = 0;
    root_dir->parent = root_dir; 
    root_dir->first_child = NULL;
    root_dir->next_sibling = NULL;
    current_dir = root_dir;
}

static void create_vfs_node(const char* path, int is_dir) {
    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    int len = strlen(path_copy);
    while (len > 0 && path_copy[len - 1] == '/') {
        path_copy[len - 1] = '\0';
        len--;
    }
    if (len == 0) {
        if (is_dir) VFS_PRINT("mkdir: cannot create directory: Invalid name\n");
        return;
    }

    char* last_slash = strrchr(path_copy, '/');
    char* node_name;
    VFSNode* target_parent;

    if (last_slash != NULL) {
        *last_slash = '\0';
        node_name = last_slash + 1;
        
        if (strlen(path_copy) == 0) {
            target_parent = root_dir; 
        } else {
            target_parent = resolve_path(path_copy);
        }
    } else {
        node_name = path_copy;
        target_parent = current_dir;
    }

    if (target_parent == NULL) {
        VFS_PRINT("%s: cannot create '%s': No such file or directory\n", is_dir ? "mkdir" : "touch", path);
        return;
    }
    if (!target_parent->is_dir) {
        VFS_PRINT("%s: cannot create '%s': Not a directory\n", is_dir ? "mkdir" : "touch", path);
        return;
    }

    if (is_dir && (strcmp(node_name, ".") == 0 || strcmp(node_name, "..") == 0)) {
        VFS_PRINT("mkdir: cannot create directory '%s': Invalid name\n", node_name);
        return;
    }

    VFSNode* child = target_parent->first_child;
    while (child != NULL) {
        if (strncmp(child->name, node_name, MAX_NAME_LEN - 1) == 0) {
            if (is_dir) VFS_PRINT("mkdir: cannot create directory '%s': File exists\n", path);
            return;
        }
        child = child->next_sibling;
    }

    VFSNode* new_node = NULL;
    for (int i = 0; i < MAX_NODES; i++) {
        if (!node_pool[i].is_active) {
            new_node = &node_pool[i];
            break;
        }
    }

    if (new_node == NULL) {
        VFS_PRINT("%s: VFS out of memory\n", is_dir ? "mkdir" : "touch");
        return;
    }

    new_node->start_block = -1;
    new_node->size = 0;
    new_node->is_active = 1;
    strncpy(new_node->name, node_name, MAX_NAME_LEN - 1);
    new_node->name[MAX_NAME_LEN - 1] = '\0';
    new_node->is_dir = is_dir; 
    new_node->parent = target_parent;
    new_node->first_child = NULL;
    new_node->next_sibling = target_parent->first_child;
    target_parent->first_child = new_node;
}
void vfs_mkdir_p(const char* path) {
    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char* p = path_copy;
    // Skip the leading slash if it's an absolute path
    if (*p == '/') p++; 

    // Walk down the path string
    while (*p) {
        if (*p == '/') {
            *p = '\0'; // Temporarily chop the string here
            
            // If this parent directory doesn't exist, create it
            if (resolve_path(path_copy) == NULL) {
                vfs_mkdir(path_copy);
            }
            *p = '/'; // Restore the slash and keep going
        }
        p++;
    }
    
    // Create the final directory at the end of the path
    if (resolve_path(path_copy) == NULL) {
        vfs_mkdir(path_copy);
    }
}

void vfs_clear() {
#ifdef ARDUINO
    terminal_clear();
#else
    // Fallback for when you compile natively on Linux
    printf("\033[2J\033[H"); 
#endif
}

void vfs_mkdir(const char* path) {
    create_vfs_node(path, 1);
}

void vfs_touch(const char* path) {
    create_vfs_node(path, 0);
}
void vfs_mv(const char* src_path, const char* dest_path) {
    VFSNode* src_node = resolve_path(src_path);
    if (src_node == NULL) {
        VFS_PRINT("mv: cannot stat '%s': No such file or directory\n", src_path);
        return;
    }
    if (src_node == root_dir) {
        VFS_PRINT("mv: cannot move root directory\n");
        return;
    }

    char dest_copy[256];
    strncpy(dest_copy, dest_path, sizeof(dest_copy) - 1);
    dest_copy[sizeof(dest_copy) - 1] = '\0';
    
    int len = strlen(dest_copy);
    while (len > 0 && dest_copy[len - 1] == '/') dest_copy[--len] = '\0';

    VFSNode* target_parent = NULL;
    char* new_name = NULL;
    VFSNode* dest_node = resolve_path(dest_path);

    // Determine if destination is an existing directory or a new path/name
    if (dest_node != NULL) {
        if (dest_node->is_dir) {
            target_parent = dest_node;
            new_name = src_node->name; 
        } else {
            if (dest_node == src_node) return; 
            vfs_rm(dest_path, 0); // Overwrite existing file
            char* last_slash = strrchr(dest_copy, '/');
            if (last_slash != NULL) {
                *last_slash = '\0';
                new_name = last_slash + 1;
                target_parent = (strlen(dest_copy) == 0) ? root_dir : resolve_path(dest_copy);
            } else {
                new_name = dest_copy;
                target_parent = current_dir;
            }
        }
    } else {
        char* last_slash = strrchr(dest_copy, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
            new_name = last_slash + 1;
            target_parent = (strlen(dest_copy) == 0) ? root_dir : resolve_path(dest_copy);
        } else {
            new_name = dest_copy;
            target_parent = current_dir;
        }
    }

    if (target_parent == NULL || !target_parent->is_dir) {
        VFS_PRINT("mv: cannot move to '%s': Not a directory\n", dest_path);
        return;
    }

    // Prevent infinite loops (moving a directory into itself)
    VFSNode* check_ancestor = target_parent;
    while (check_ancestor != NULL) {
        if (check_ancestor == src_node) {
            VFS_PRINT("mv: cannot move a directory into itself\n");
            return;
        }
        if (check_ancestor == root_dir) break;
        check_ancestor = check_ancestor->parent;
    }

    // Check for collisions
    VFSNode* child = target_parent->first_child;
    while (child != NULL) {
        if (strncmp(child->name, new_name, MAX_NAME_LEN - 1) == 0 && child != src_node) {
            VFS_PRINT("mv: cannot move '%s': File exists\n", src_path);
            return;
        }
        child = child->next_sibling;
    }

    // 1. Unlink from current parent
    VFSNode* prev = NULL;
    VFSNode* curr = src_node->parent->first_child;
    while (curr != NULL) {
        if (curr == src_node) {
            if (prev == NULL) src_node->parent->first_child = src_node->next_sibling;
            else prev->next_sibling = src_node->next_sibling;
            break;
        }
        prev = curr;
        curr = curr->next_sibling;
    }

    // 2. Update properties and link to new parent
    strncpy(src_node->name, new_name, MAX_NAME_LEN - 1);
    src_node->name[MAX_NAME_LEN - 1] = '\0';
    src_node->parent = target_parent;
    src_node->next_sibling = target_parent->first_child;
    target_parent->first_child = src_node;
}
void vfs_cd(const char* path) {
    VFSNode* target = resolve_path(path);
    if (target != NULL) {
        if (target->is_dir) {
            current_dir = target;
        } else {
            VFS_PRINT("cd: %s: Not a directory\n", path);
        }
    } else {
        VFS_PRINT("cd: %s: No such file or directory\n", path);
    }
}

void vfs_ls(const char* path) {
    VFSNode* target = resolve_path(path);
    if (target == NULL) {
        VFS_PRINT("ls: cannot access '%s': No such file or directory\n", path);
        return;
    }

    if (!target->is_dir) {
        VFS_PRINT("%s\n", target->name);
        return;
    }

    VFSNode* child = target->first_child;
    while (child != NULL) {
        if (child->is_dir) {
            VFS_PRINT("%s/  ", child->name);
        } else {
            VFS_PRINT("%s  ", child->name);
        }
        child = child->next_sibling;
    }
    VFS_PRINT("\n");
}
// Helper to safely free a single node's memory blocks
static void free_node_memory(VFSNode* target) {
    int curr_block = target->start_block;
    while (curr_block != -1) {
        int next = block_pool[curr_block].next_block;
        block_pool[curr_block].is_active = 0;
        curr_block = next;
    }
    target->is_active = 0;
}

// Recursive helper to delete children
static void vfs_rm_recursive(VFSNode* target) {
    VFSNode* child = target->first_child;
    while (child != NULL) {
        VFSNode* next = child->next_sibling;
        if (child->is_dir) {
            vfs_rm_recursive(child);
        } else {
            free_node_memory(child);
        }
        child = next;
    }
    free_node_memory(target);
}
void vfs_rm(const char* path, int recursive) {
    VFSNode* target = resolve_path(path);
    
    if (target == NULL) {
        VFS_PRINT("rm: cannot remove '%s': No such file or directory\n", path);
        return;
    }
    if (target == root_dir) {
        VFS_PRINT("rm: cannot remove root directory\n");
        return;
    }
    if (target == current_dir) {
        VFS_PRINT("rm: cannot remove current working directory\n");
        return;
    }
    if (target->first_child != NULL && !recursive) {
        VFS_PRINT("rm: cannot remove '%s': Directory not empty (use -r)\n", path);
        return;
    }

    // Unlink from parent
    VFSNode* prev = NULL;
    VFSNode* curr = target->parent->first_child;
    while (curr != NULL) {
        if (curr == target) {
            if (prev == NULL) target->parent->first_child = target->next_sibling;
            else prev->next_sibling = target->next_sibling;
            break;
        }
        prev = curr;
        curr = curr->next_sibling;
    }

    // Execute deletion
    if (target->is_dir && recursive) {
        vfs_rm_recursive(target);
    } else {
        free_node_memory(target);
    }
}
int allocate_block() {
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (!block_pool[i].is_active) {
            block_pool[i].is_active = 1;
            block_pool[i].next_block = -1;
            memset(block_pool[i].data, 0, BLOCK_SIZE);
            return i;
        }
    }
    return -1; 
}

void vfs_write(const char* path, const char* text) {
    VFSNode* target = resolve_path(path);
    if (target == NULL || target->is_dir) {
        VFS_PRINT("write: cannot write to '%s'\n", path);
        return;
    }

    int curr = target->start_block;
    while (curr != -1) {
        int next = block_pool[curr].next_block;
        block_pool[curr].is_active = 0;
        curr = next;
    }
    target->start_block = -1;
    target->size = 0;

    int text_len = strlen(text);
    int written = 0;
    int last_block = -1;

    while (written < text_len) {
        int b = allocate_block();
        if (b == -1) {
            VFS_PRINT("write: VFS out of space\n");
            break;
        }
        
        if (target->start_block == -1) target->start_block = b;
        if (last_block != -1) block_pool[last_block].next_block = b;
        
        int chunk = text_len - written;
        if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE; 
        
        strncpy(block_pool[b].data, text + written, chunk);
        written += chunk;
        target->size += chunk;
        last_block = b;
    }
}

void vfs_cat(const char* path) {
    VFSNode* target = resolve_path(path);
    if (target == NULL || target->is_dir) {
        VFS_PRINT("cat: cannot read '%s'\n", path);
        return;
    }
    
    int curr = target->start_block;
    while (curr != -1) {
        for (int i = 0; i < BLOCK_SIZE && block_pool[curr].data[i] != '\0'; i++) {
            VFS_PRINT("%c", block_pool[curr].data[i]);
        }
        curr = block_pool[curr].next_block;
    }
    VFS_PRINT("\n");
}
void vfs_help() {
    VFS_PRINT("Cmds: mkdir, touch, ls, cd\n");
    VFS_PRINT("rm, mv, write, cat, free\n");
    VFS_PRINT("whoami, clear, help\n");
    VFS_PRINT("Tip: <cmd> --help\n");
}
void vfs_free() {
    int used_nodes = 0;
    int used_blocks = 0;

    for (int i = 0; i < MAX_NODES; i++) {
        if (node_pool[i].is_active) used_nodes++;
    }
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (block_pool[i].is_active) used_blocks++;
    }

    // Line 1: Static VFS Allocation
    VFS_PRINT("VFS: Nodes %d/%d, Blks %d/%d\n", used_nodes, MAX_NODES, used_blocks, MAX_BLOCKS);

#ifdef ARDUINO
    // Line 2: Hardware SRAM (Converted to KB for brevity)
    int free_kb = ESP.getFreeHeap() / 1024;
    int max_kb = ESP.getMaxAllocHeap() / 1024;
    VFS_PRINT("RAM: %dKB Free (Max: %dKB)\n", free_kb, max_kb);
#endif
}
void parse_and_execute(char *input) {
    char *argv[MAX_ARGS];
    int argc = 0;

    input[strcspn(input, "\n")] = 0;

    char *saveptr_args;
    char *token = strtok_r(input, " ", &saveptr_args);
    while (token != NULL && argc < MAX_ARGS - 1) {
        argv[argc++] = token;
        token = strtok_r(NULL, " ", &saveptr_args);
    }
    argv[argc] = NULL;
    if (argc == 0) return;
    // --- NEW: Context-Aware --help Interceptor ---
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        if (strcmp(argv[0], "mkdir") == 0) VFS_PRINT("Usage: mkdir [-p] <dir>\n");
        else if (strcmp(argv[0], "touch") == 0) VFS_PRINT("Usage: touch <file>\n");
        else if (strcmp(argv[0], "ls") == 0) VFS_PRINT("Usage: ls [dir]\n");
        else if (strcmp(argv[0], "cd") == 0) VFS_PRINT("Usage: cd <dir>\n");
        else if (strcmp(argv[0], "rm") == 0) VFS_PRINT("Usage: rm [-r] <path>\n");
        else if (strcmp(argv[0], "mv") == 0) VFS_PRINT("Usage: mv <src> <dest>\n");
        else if (strcmp(argv[0], "write") == 0) VFS_PRINT("Usage: write <file> <text>\n");
        else if (strcmp(argv[0], "cat") == 0) VFS_PRINT("Usage: cat <file>\n");
        else VFS_PRINT("No help available for '%s'\n", argv[0]);
        return;
    }
    // ---------------------------------------------

    if (strcmp(argv[0], "mkdir") == 0) {
        if (argc > 1) {
            if (strcmp(argv[1], "-p") == 0) {
                if (argc > 2) {
                    vfs_mkdir_p(argv[2]);
                } else {
                    VFS_PRINT("mkdir: missing operand\n");
                }
            } else {
                vfs_mkdir(argv[1]);
            }
        } else {
            VFS_PRINT("mkdir: missing operand\n");
        }
    }
    else if (strcmp(argv[0], "clear") == 0) {
        vfs_clear();
    }
    else if (strcmp(argv[0], "touch") == 0) {
        if (argc > 1) {
            vfs_touch(argv[1]);
        } else {
            VFS_PRINT("touch: missing file operand\n");
        }
    }
    else if (strcmp(argv[0], "help") == 0) {
        vfs_help();
    }
    else if (strcmp(argv[0], "ls") == 0) {
        if (argc > 1) {
            vfs_ls(argv[1]);
        } else {
            vfs_ls(NULL); 
        }
    } 
    else if (strcmp(argv[0], "cd") == 0) {
        if (argc > 1) {
            vfs_cd(argv[1]);
        } else {
            vfs_cd("/"); 
        }
    }
    else if (strcmp(argv[0], "rm") == 0) {
        if (argc > 1) {
            if (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "-R") == 0) {
                if (argc > 2) vfs_rm(argv[2], 1);
                else VFS_PRINT("rm: missing operand\n");
            } else {
                vfs_rm(argv[1], 0);
            }
        } else {
            VFS_PRINT("rm: missing operand\n");
        }
    }
    else if (strcmp(argv[0], "mv") == 0) {
        if (argc > 2) {
            vfs_mv(argv[1], argv[2]);
        } else {
            VFS_PRINT("mv: missing destination operand\n");
        }
    }
    else if (strcmp(argv[0], "write") == 0) {
        if (argc > 2) {
            char text_buf[256] = {0};
            size_t current_len = 0;
            
            for (int i = 2; i < argc; i++) {
                size_t arg_len = strlen(argv[i]);
                
                if (current_len + arg_len + 2 > sizeof(text_buf)) {
                    VFS_PRINT("write: argument list too long (max %zu bytes)\n", sizeof(text_buf) - 1);
                    break; 
                }
                
                strcat(text_buf, argv[i]);
                current_len += arg_len;
                
                if (i < argc - 1) {
                    strcat(text_buf, " ");
                    current_len++;
                }
            }
            vfs_write(argv[1], text_buf);
        } else {
            VFS_PRINT("write: missing operand\n");
        }
    }
    else if (strcmp(argv[0], "cat") == 0) {
        if (argc > 1) vfs_cat(argv[1]);
        else VFS_PRINT("cat: missing operand\n");
    }
    else if (strcmp(argv[0], "whoami") == 0) {
        VFS_PRINT("root\n");
    }
    else if (strcmp(argv[0], "free") == 0) {
        vfs_free();
    }
    else {
        VFS_PRINT("%s: command not found\n", argv[0]);
    }
}
