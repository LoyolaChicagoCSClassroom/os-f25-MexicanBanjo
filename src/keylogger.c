// src/keylogger.c
#include <stdint.h>
#include "rprintf.h"
#include "keylogger.h"

// kputc is defined in kernel_main.c;
// we just declare it here so the linker can use it.
extern int kputc(int data);

#define KEYLOG_BUF_SIZE 1024

static char keylog_buf[KEYLOG_BUF_SIZE];
static uint16_t keylog_head = 0;   // next write index
static uint8_t  keylog_full = 0;   // has wrapped at least once?

void keylog_init(void) {
    keylog_head = 0;
    keylog_full = 0;
    for (uint16_t i = 0; i < KEYLOG_BUF_SIZE; i++) {
        keylog_buf[i] = 0;
    }
}

void keylog_add_char(char c) {
    // Optional: only log printable ASCII + newline
    if ((c < 0x20 || c > 0x7e) && c != '\n' && c != '\r') {
        return;
    }

    keylog_buf[keylog_head] = c;
    keylog_head++;

    if (keylog_head >= KEYLOG_BUF_SIZE) {
        keylog_head = 0;
        keylog_full = 1;
    }
}

void keylog_dump(void) {
    uint16_t start;
    uint16_t count;

    if (!keylog_full && keylog_head == 0) {
        esp_printf(kputc, "Keylog is empty.\n");
        return;
    }

    if (keylog_full) {
        start = keylog_head;          // oldest entry
        count = KEYLOG_BUF_SIZE;
    } else {
        start = 0;
        count = keylog_head;
    }

    esp_printf(kputc, "=== KEYLOG START ===\n");

    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = (start + i) % KEYLOG_BUF_SIZE;
        char c = keylog_buf[idx];

        if (c == 0) continue;
        if (c == '\r') continue;

        kputc(c);
    }

    esp_printf(kputc, "\n=== KEYLOG END ===\n");
}
