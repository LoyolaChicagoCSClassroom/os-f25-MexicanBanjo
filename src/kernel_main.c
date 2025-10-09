
#include <stdint.h>
#include "rprintf.h"

#define MEMORY 0xB8000
#define WIDTH  80
#define HEIGHT 25
#define MULTIBOOT2_HEADER_MAGIC        0xe85250d6

const unsigned int multiboot_header[]  __attribute__((section(".multiboot"))) = {MULTIBOOT2_HEADER_MAGIC, 0, 16, -(16+MULTIBOOT2_HEADER_MAGIC), 0, 12};

static int cursor_row = 0;
static int cursor_col = 0;
static const uint8_t vga_color = 0x07;

int putc(int data) { //Deliverable 1.
    volatile uint16_t *vram = (uint16_t*)MEMORY;
    if (data == '\n') {
        cursor_col = 0; 
        cursor_row++; 
    } else if (data == '\r') { 
        cursor_col = 0; 
    } else {
        int pos = cursor_row * WIDTH + cursor_col;
        vram[pos] = (vga_color << 8) | (uint8_t)data;
        cursor_col++;
        if (cursor_col >= WIDTH) { 
            cursor_col = 0; 
            cursor_row++;
        }
    }

    if (cursor_row >= HEIGHT) {
        for (int r = 1; r < HEIGHT; r++)
            for (int c = 0; c < WIDTH; c++)
                vram[(r - 1)* WIDTH + c] = vram[r * WIDTH + c];
        for (int c = 0; c < WIDTH; c++)
            vram[(HEIGHT - 1) * WIDTH + c] = (vga_color << 8) | ' ';
        cursor_row = HEIGHT-1;
        cursor_col = 0;
    }
    return data;
}

void main() {
    esp_printf(putc, "Kernel initialized.\n");
    esp_printf(putc, "Current execution level: %d\n", 0); // Prints current execution. Deliverable 2.
    //for (int i = 0; i < 30; i++) { // THIS IS FOR TESTING SCROLL. Deliverable 3.
        //esp_printf(putc, "Line %d: This is a test of the terminal scroll.\n", i);
    //}
    while(1) { }
}
