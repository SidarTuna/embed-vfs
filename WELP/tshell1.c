#include <stdio.h>
#include <string.h>

#define MAX_ARGS 64
#define MAX_NODES 64       // Static limit to fit in ESP32 RAM
#define MAX_NAME_LEN 16
#define BLOCK_SIZE 64
#define MAX_BLOCKS 256 // 16KB total block pool RAM

//FUNTION PROTOTYPES


// New Data Block Structure
typedef struct DataBlock {
    int is_active;
    int next_block; // -1 if it's the end of the file
    char data[BLOCK_SIZE];
} DataBlock;
// Static Block Pool
DataBlock block_pool[MAX_BLOCKS];
// 1. Define the VFS Node structure
typedef struct VFSNode {
    char name[MAX_NAME_LEN];
    int is_dir;
    int is_active;
    int start_block; // -1 if empty or a directory
    int size;        // file size in bytes
    struct VFSNode* parent;
    struct VFSNode* first_child;
    struct VFSNode* next_sibling;
} VFSNode;
// Static Memory Pool
VFSNode node_pool[MAX_NODES];
int node_count = 0;

VFSNode* root_dir;
VFSNode* current_dir;
//FUNCTION PROTOTYPES 
VFSNode* resolve_path(const char* path);
void init_vfs();
static void create_vfs_node(const char* path, int is_dir);
void vfs_mkdir(const char* path);
void vfs_touch(const char* path);
void vfs_cd(const char* path);
void vfs_ls(const char* path);
void vfs_rm(const char* path);
int allocate_block();
void vfs_write(const char* path, const char* text);
void vfs_cat(const char* path);
void parse_and_execute(char *input);
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
        // Look ahead to see if this is the final token
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
                    // Match must be a directory UNLESS it is the last token in the path
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
// 3. VFS API Functions
void init_vfs() {
    for (int i = 0; i < MAX_NODES; i++) node_pool[i].is_active = 0;
    for (int i = 0; i < MAX_BLOCKS; i++) block_pool[i].is_active = 0; // Clear blocks
    
    root_dir = &node_pool[0];
    root_dir->is_active = 1;
    strcpy(root_dir->name, "/");
    root_dir->is_dir = 1;
    root_dir->start_block = -1; // Directories have no data blocks
    root_dir->size = 0;
    root_dir->parent = root_dir; 
    root_dir->first_child = NULL;
    root_dir->next_sibling = NULL;
    current_dir = root_dir;
}
// Internal helper to handle both file and directory creation
static void create_vfs_node(const char* path, int is_dir) {
    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    // Strip trailing slashes
    int len = strlen(path_copy);
    while (len > 0 && path_copy[len - 1] == '/') {
        path_copy[len - 1] = '\0';
        len--;
    }
    if (len == 0) {
        if (is_dir) printf("mkdir: cannot create directory: Invalid name\n");
        return;
    }

    // Split into parent path and new node name
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
        printf("%s: cannot create '%s': No such file or directory\n", is_dir ? "mkdir" : "touch", path);
        return;
    }
    if (!target_parent->is_dir) {
        printf("%s: cannot create '%s': Not a directory\n", is_dir ? "mkdir" : "touch", path);
        return;
    }

    // Block reserved names for directories
    if (is_dir && (strcmp(node_name, ".") == 0 || strcmp(node_name, "..") == 0)) {
        printf("mkdir: cannot create directory '%s': Invalid name\n", node_name);
        return;
    }

    // Prevent duplicates
    VFSNode* child = target_parent->first_child;
    while (child != NULL) {
        if (strncmp(child->name, node_name, MAX_NAME_LEN - 1) == 0) {
            if (is_dir) printf("mkdir: cannot create directory '%s': File exists\n", path);
            return; // Touch silently exits if file exists
        }
        child = child->next_sibling;
    }

    // Allocate from static pool
    VFSNode* new_node = NULL;
    for (int i = 0; i < MAX_NODES; i++) {
        if (!node_pool[i].is_active) {
            new_node = &node_pool[i];
            break;
        }
    }

    if (new_node == NULL) {
        printf("%s: VFS out of memory\n", is_dir ? "mkdir" : "touch");
        return;
    }

    // Initialize node
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

// The exposed API just calls the helper with the correct flag
void vfs_mkdir(const char* path) {
    create_vfs_node(path, 1);
}

void vfs_touch(const char* path) {
    create_vfs_node(path, 0);
}
void vfs_cd(const char* path) {
    VFSNode* target = resolve_path(path);
    if (target != NULL) {
        if (target->is_dir) {
            current_dir = target;
        } else {
            printf("cd: %s: Not a directory\n", path);
        }
    } else {
        printf("cd: %s: No such file or directory\n", path);
    }
}

void vfs_ls(const char* path) {
    VFSNode* target = resolve_path(path);
    if (target == NULL) {
        printf("ls: cannot access '%s': No such file or directory\n", path);
        return;
    }

    // If the target is a file, just print its name
    if (!target->is_dir) {
        printf("%s\n", target->name);
        return;
    }

    VFSNode* child = target->first_child;
    while (child != NULL) {
        if (child->is_dir) {
            printf("%s/  ", child->name);
        } else {
            printf("%s  ", child->name);
        }
        child = child->next_sibling;
    }
    printf("\n");
}
void vfs_rm(const char* path) {
    VFSNode* target = resolve_path(path);
    
    if (target == NULL) {
        printf("rm: cannot remove '%s': No such file or directory\n", path);
        return;
    }
    if (target == root_dir) {
        printf("rm: cannot remove root directory\n");
        return;
    }
    if (target == current_dir) {
        printf("rm: cannot remove current working directory\n");
        return;
    }
    if (target->first_child != NULL) {
        printf("rm: cannot remove '%s': Directory not empty\n", path);
        return;
    }

    // Unlink from the parent's child/sibling chain
    VFSNode* prev = NULL;
    VFSNode* curr = target->parent->first_child;
    
    while (curr != NULL) {
        if (curr == target) {
            if (prev == NULL) {
                target->parent->first_child = target->next_sibling;
            } else {
                prev->next_sibling = target->next_sibling;
            }
            break;
        }
        prev = curr;
        curr = curr->next_sibling;
    }
    // Free associated data blocks
    int curr_block = target->start_block;
    while (curr_block != -1) {
        int next = block_pool[curr_block].next_block;
        block_pool[curr_block].is_active = 0;
        curr_block = next;
    }
    // Mark the memory slot as available
    target->is_active = 0;
}
// Helper to grab a free block
int allocate_block() {
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (!block_pool[i].is_active) {
            block_pool[i].is_active = 1;
            block_pool[i].next_block = -1;
            memset(block_pool[i].data, 0, BLOCK_SIZE);
            return i;
        }
    }
    return -1; // Out of memory
}

void vfs_write(const char* path, const char* text) {
    VFSNode* target = resolve_path(path);
    if (target == NULL || target->is_dir) {
        printf("write: cannot write to '%s'\n", path);
        return;
    }

    // 1. Free existing blocks (This simulates an overwrite '>')
    int curr = target->start_block;
    while (curr != -1) {
        int next = block_pool[curr].next_block;
        block_pool[curr].is_active = 0;
        curr = next;
    }
    target->start_block = -1;
    target->size = 0;

    // 2. Allocate blocks and write payload
    int text_len = strlen(text);
    int written = 0;
    int last_block = -1;

    while (written < text_len) {
        int b = allocate_block();
        if (b == -1) {
            printf("write: VFS out of space\n");
            break;
        }
        
        if (target->start_block == -1) target->start_block = b;
        if (last_block != -1) block_pool[last_block].next_block = b;
        
        int chunk = text_len - written;
        if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE; // Cap at block size
        
        strncpy(block_pool[b].data, text + written, chunk);
        written += chunk;
        target->size += chunk;
        last_block = b;
    }
}

void vfs_cat(const char* path) {
    VFSNode* target = resolve_path(path);
    if (target == NULL || target->is_dir) {
        printf("cat: cannot read '%s'\n", path);
        return;
    }
    
    int curr = target->start_block;
    while (curr != -1) {
        // Print character by character to respect BLOCK_SIZE bounds
        for (int i = 0; i < BLOCK_SIZE && block_pool[curr].data[i] != '\0'; i++) {
            putchar(block_pool[curr].data[i]);
        }
        curr = block_pool[curr].next_block;
    }
    printf("\n");
}
// 4. Integrated Shell Parser
void parse_and_execute(char *input) {
    char *argv[MAX_ARGS];
    int argc = 0;

    // Strip the trailing newline left by fgets
    input[strcspn(input, "\n")] = 0;

    // Tokenize the input string in-place
    char *saveptr_args;
    char *token = strtok_r(input, " ", &saveptr_args);
    while (token != NULL && argc < MAX_ARGS - 1) {
        argv[argc++] = token;
        token = strtok_r(NULL, " ", &saveptr_args);
    }
    argv[argc] = NULL;
    // Return early on empty input
    if (argc == 0) return;

    // Route commands to VFS API
    if (strcmp(argv[0], "mkdir") == 0) {
        if (argc > 1) {
            vfs_mkdir(argv[1]);
        } else {
            printf("mkdir: missing operand\n");
        }
    }
   else if (strcmp(argv[0], "touch") == 0) {
        if (argc > 1) {
            vfs_touch(argv[1]);
        } else {
            printf("touch: missing file operand\n");
        }
    }
    else if (strcmp(argv[0], "ls") == 0) {
        if (argc > 1) {
            vfs_ls(argv[1]);
        } else {
            vfs_ls(NULL); // Pass NULL for current directory
        }
    } 
    else if (strcmp(argv[0], "cd") == 0) {
        if (argc > 1) {
            vfs_cd(argv[1]);
        } else {
            vfs_cd("/"); // Default to root
        }
    }
    else if (strcmp(argv[0], "rm") == 0) {
        if (argc > 1) {
            vfs_rm(argv[1]);
        } else {
            printf("rm: missing operand\n");
        }
    }
    else if (strcmp(argv[0], "write") == 0) {
        if (argc > 2) {
            char text_buf[256] = {0};
            size_t current_len = 0;
            
            for (int i = 2; i < argc; i++) {
                size_t arg_len = strlen(argv[i]);
                
                // +2 accounts for the required space character and the null terminator
                if (current_len + arg_len + 2 > sizeof(text_buf)) {
                    printf("write: argument list too long (max %zu bytes)\n", sizeof(text_buf) - 1);
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
            printf("write: missing operand\n");
        }
    }
    else if (strcmp(argv[0], "cat") == 0) {
        if (argc > 1) vfs_cat(argv[1]);
        else printf("cat: missing operand\n");
    }
    else if (strcmp(argv[0], "whoami") == 0) {
        printf("root\n");
    } 
    else {
        // This MUST be the absolute last block in the chain
        printf("%s: command not found\n", argv[0]);
    }
}
int main() {
    init_vfs();
    char buffer[256];

    while (1) {
        printf("os:%s> ", current_dir->name);

        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }

        // Flush stdin if the input was longer than the buffer
        if (strchr(buffer, '\n') == NULL) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {
                // Discard extra characters
            }
            printf("shell: input too long, truncated to 255 chars\n");
        }

        parse_and_execute(buffer);
    }

    return 0;
}
