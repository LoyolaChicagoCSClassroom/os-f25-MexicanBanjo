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

// Multiboot header required by GRUB
const unsigned int multiboot_header[]  __attribute__((section(".multiboot"))) =
    {MULTIBOOT2_HEADER_MAGIC, 0, 16, -(16+MULTIBOOT2_HEADER_MAGIC), 0, 12};

// VGA text mode cursor control
static int cursor_row = 0;
static int cursor_col = 0;
static const uint8_t vga_color = 0x07;

int kputc(int data) {
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
                vram[(r - 1) * WIDTH + c] = vram[r * WIDTH + c];
        for (int c = 0; c < WIDTH; c++)
            vram[(HEIGHT - 1) * WIDTH + c] = (vga_color << 8) | ' ';
        cursor_row = HEIGHT - 1;
        cursor_col = 0;
    }
    return data;
}

// Entry point after GRUB loads the kernel
void main() {
    remap_pic();
    load_gdt();
    init_idt();
    esp_printf(kputc, "Initializing interrupts...\n");
    asm("sti");
    esp_printf(kputc, "Kernel initialized.\n");

    esp_printf(kputc, "Current execution level: %d\n", 0);

    // --- Initialize the physical frame allocator ---
    init_pfa_list();
    esp_printf(kputc, "Physical frame allocator ready.\n");

    // =====================================================
    // =============== Paging Test Starts Here =============
    // =====================================================

    esp_printf(kputc, "Setting up identity mapping for kernel...\n");

    // Identity-map the first 4MB (enough for kernel code + stack)
    struct ppage *id_pages = allocate_physical_pages(1024); // 4MB / 4KB = 1024 pages
    map_pages((void*)0x00000000, id_pages, pd);
    esp_printf(kputc, "Identity-mapped first 4MB of memory.\n");

    // Map 3 new test pages starting at 0xC0000000
    struct ppage *pglist = allocate_physical_pages(3);
    if (!pglist) {
        esp_printf(kputc, "Failed to allocate test pages!\n");
        while (1);
    }

    void *virt = (void*)0xC0000000;
    esp_printf(kputc, "Mapping 3 test pages starting at virtual 0x%p...\n", virt);
    map_pages(virt, pglist, pd);

    // Load the page directory and enable paging
    loadPageDirectory(pd);
    enable_paging();

    esp_printf(kputc, "Paging enabled successfully.\n");

    // Test writing/reading through the new mapping
    uint32_t *test_ptr = (uint32_t*)virt;
    test_ptr[0] = 0xDEADBEEF;
    test_ptr[1] = 0xCAFEBABE;

    uint32_t a = test_ptr[0];
    uint32_t b = test_ptr[1];

    esp_printf(kputc, "Wrote and read test values:\n");
    esp_printf(kputc, "  a = 0x%x\n", a);
    esp_printf(kputc, "  b = 0x%x\n", b);

    // Free test pages (optional cleanup)
    free_physical_pages(pglist);
    esp_printf(kputc, "Freed test pages.\n");

    esp_printf(kputc, "Paging test complete.\n");

    // =====================================================
    // =============== End of Paging Test ==================
    // =====================================================

    while (1) { } // stay alive
}
