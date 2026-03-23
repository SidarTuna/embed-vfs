#include <Arduino.h>
#include <TFT_eSPI.h>
#include "vfs.h"
#include <stdarg.h>

#define MAX_CMD_LEN 256
char cmd_buffer[MAX_CMD_LEN];
int cmd_index = 0;

TFT_eSPI tft = TFT_eSPI();

// Screen cursor tracking
int cursor_y = 0;
const int font_height = 16; // Font 2 is 16px high

// The Dual-Router function
void terminal_print(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // 1. Send to PC Terminal
    Serial.print(buffer);

    // 2. Send to LilyGo TFT Screen
    for (int i = 0; buffer[i] != '\0'; i++) {
        if (buffer[i] == '\n') {
            cursor_y += font_height;
            tft.setCursor(0, cursor_y);
        } else if (buffer[i] == '\r') {
            tft.setCursor(0, cursor_y);
        } else if (buffer[i] == '\b') {
            // Simplified backspace for V1 (ignores drawing a black box for now)
            int current_x = tft.getCursorX();
            if (current_x > 0) tft.setCursor(current_x - tft.textWidth(" "), cursor_y);
        } else {
            tft.print(buffer[i]);
        }
    }

    // Screen wrap: wipe clean if we hit the bottom
    if (cursor_y >= tft.height() - font_height) {
        delay(500); // Brief pause so you can read the last line
        tft.fillScreen(TFT_BLACK);
        cursor_y = 0;
        tft.setCursor(0, 0);
    }
}
// Clears both the PC terminal and the physical TFT screen
void terminal_clear() {
    // 1. Send ANSI escape code to clear the PC serial monitor
    Serial.print("\033[2J\033[H");

    // 2. Wipe the LilyGo TFT screen and reset the cursor
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    cursor_y = 0;
}
void setup() {
    Serial.begin(115200);
    
    // Initialize the TFT Screen
    tft.init();
    tft.setRotation(1); // Landscape mode
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK); // Classic retro terminal colors
    tft.setTextFont(2);
    tft.setCursor(0, 0);

    delay(1000); 

    // Boot the Virtual File System
    init_vfs();
    
    // Hidden debug message for the PC terminal
    Serial.println("\n[SYSTEM] ESP32 VFS Booted Successfully.");
    
    // Minimal boot message for the LilyGo TFT
    terminal_print("ESP32 VFS v1.0\n");
    terminal_print("SRAM: %d B Free\n", ESP.getFreeHeap());
    terminal_print("Type 'help' for cmds\n\n");
    
    // Print the initial shell prompt
    VFS_PRINT("os:%s> ", current_dir->name);
}


// Keep your exact existing loop() function down here...
void loop() {
    if (Serial.available() > 0) {
        char c = Serial.read();
        static char last_c = '\0'; 

        if (c == '\n' && last_c == '\r') {
            last_c = c;
            return; 
        }

        if (c == '\n' || c == '\r') {
            VFS_PRINT("\n");
            cmd_buffer[cmd_index] = '\0';
            
            if (cmd_index > 0) {
                parse_and_execute(cmd_buffer);
                cmd_index = 0;
            }
            
            VFS_PRINT("os:%s> ", current_dir->name);
        }
        else if (c == '\b' || c == 0x7F) {
            if (cmd_index > 0) {
                cmd_index--;
                VFS_PRINT("\b \b"); 
            }
        }
        else if (cmd_index < MAX_CMD_LEN - 1) {
            cmd_buffer[cmd_index++] = c;
            VFS_PRINT("%c", c); 
        }

        last_c = c; 
    }
}
