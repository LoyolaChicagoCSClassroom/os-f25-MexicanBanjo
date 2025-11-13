
#include <stdint.h>
#include <stdio.h>
#include "rprintf.h"
#include "interrupt.h"
#include "page.h"
#include "paging.h"

#define MEMORY 0xB8000
#define WIDTH  80
#define HEIGHT 25
#define MULTIBOOT2_HEADER_MAGIC        0xe85250d6

const unsigned int multiboot_header[]  __attribute__((section(".multiboot"))) = {MULTIBOOT2_HEADER_MAGIC, 0, 16, -(16+MULTIBOOT2_HEADER_MAGIC), 0, 12};

static int cursor_row = 0;
static int cursor_col = 0;
static const uint8_t vga_color = 0x07;

int kputc(int data) { //Deliverable 1.
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
    remap_pic();
    load_gdt();
    init_idt();
    esp_printf(kputc, "Initializing interrupts...\n");
    asm("sti");
    esp_printf(kputc, "Kernel initialized.\n");

    esp_printf(kputc, "Current execution level: %d\n", 0);

    // Initialize physical frame allocator (your existing function)
    init_pfa_list();
    esp_printf(kputc, "Physical frame allocator ready.\n");

    // ✅ Allocate a small list of 3 pages
    struct ppage *pglist = allocate_physical_pages(3);
    if (!pglist) {
        esp_printf(kputc, "Failed to allocate test pages.\n");
        while(1);
    }

    // ✅ Pick a virtual address to map them to
    void *virt = (void*)0xC0000000; // 3 pages starting here

    esp_printf(kputc, "Mapping pages starting at virtual 0x%p\n", virt);

    // ✅ Call your map_pages() using the global page directory
    map_pages(virt, pglist, pd);

    // ✅ Load page directory and enable paging
    loadPageDirectory(pd);
    enable_paging();

    esp_printf(kputc, "Paging enabled.\n");

    // ✅ Test writing/reading to the mapped virtual memory
    uint32_t *test_ptr = (uint32_t*)virt;
    test_ptr[0] = 0xDEADBEEF;
    test_ptr[1] = 0xCAFEBABE;

    esp_printf(kputc, "Wrote test values to mapped pages.\n");

    // Read them back
    uint32_t a = test_ptr[0];
    uint32_t b = test_ptr[1];
    esp_printf(kputc, "Read back values: a=0x%x, b=0x%x\n", a, b);

    // ✅ Cleanup: free physical pages after test
    free_physical_pages(pglist);
    esp_printf(kputc, "Freed test pages.\n");

    // Halt kernel (or loop forever)
    while(1) { }
}
