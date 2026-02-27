# embed-vfs

A zero-allocation, inode-based Virtual File System (VFS) designed for memory-constrained embedded systems.

## Project Overview
This project implements a lightweight file system in pure C, specifically designed to run on microcontrollers like the **ESP32** or **STM32** without relying on dynamic memory allocation (`malloc`).

## Roadmap
* **Bare-Metal ESP32 Port:** Migrating the core VFS logic to run natively on the ESP32 using the ESP-IDF framework, replacing standard I/O with UART communication.
* **Network Integration:** Implementing a streaming HTTP client to fetch web content directly into the VFS block pool.
* **Live HTML Scraping:** Building a real-time Finite State Machine (FSM) to strip HTML tags and metadata from incoming network packets, allowing for a text-based browsing experience on a 16KB memory footprint.
