#ifndef VFS_H
#define VFS_H

#include <string.h>

#ifdef ARDUINO
  #include <Arduino.h>
  extern void terminal_print(const char* format, ...);
  extern void terminal_clear(); // Add this line
  #define VFS_PRINT(...) terminal_print(__VA_ARGS__)
#else
  #include <stdio.h>
  #define VFS_PRINT(...) printf(__VA_ARGS__)
#endif

#define MAX_ARGS 64
#define MAX_NODES 64       // Static limit to fit in ESP32 RAM
#define MAX_NAME_LEN 16
#define BLOCK_SIZE 64
#define MAX_BLOCKS 256     // 16KB total block pool RAM

typedef struct DataBlock {
    int is_active;
    int next_block; // -1 if it's the end of the file
    char data[BLOCK_SIZE];
} DataBlock;

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

// Expose current_dir so main.cpp can print the shell prompt
extern VFSNode* current_dir;

// API Prototypes
void init_vfs();
void vfs_mkdir(const char* path);
void vfs_mkdir_p(const char* path);
void vfs_touch(const char* path);
void vfs_cd(const char* path);
void vfs_ls(const char* path);
void vfs_rm(const char* path, int recursive);
void vfs_mv(const char* src_path, const char* dest_path);
void vfs_write(const char* path, const char* text);
void vfs_cat(const char* path);
void vfs_free();
void vfs_help();
void vfs_clear();
void parse_and_execute(char *input);

#endif // VFS_H
