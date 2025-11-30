// keylogger.c
#include <stdint.h>
#include "rprintf.h"
#include "keylogger.h"
#include "serial.h"     // <---- NEW include

extern int kputc(int); // VGA output

#define KEYLOG_BUF_SIZE 1024

static char keylog_buf[KEYLOG_BUF_SIZE];
static uint16_t keylog_head = 0;   // next write position
static uint8_t  keylog_full = 0;   // has buffer wrapped?

void keylog_init(void) {
    keylog_head = 0;
    keylog_full = 0;
    for (uint16_t i = 0; i < KEYLOG_BUF_SIZE; i++) {
        keylog_buf[i] = 0;
    }
}

/*
 * Log one character:
 * 1. Save to ring buffer (for F12 dumping)
 * 2. Send out of COM1 so QEMU writes it to host log file
 */
void keylog_add_char(char c) {

    // Filter out non-printable characters except newline
    if ((c < 0x20 || c > 0x7e) && c != '\n' && c != '\r') {
        return;
    }

    // 1. Save to ring buffer
    keylog_buf[keylog_head] = c;
    keylog_head++;

    if (keylog_head >= KEYLOG_BUF_SIZE) {
        keylog_head = 0;
        keylog_full = 1;
    }

    // 2. Send to host via COM1 serial port
    serial_write(c);
}

void keylog_dump(void) {
    uint16_t start, count;

    if (!keylog_full && keylog_head == 0) {
        esp_printf(kputc, "Keylog is empty.\n");
        return;
    }

    if (keylog_full) {
        start = keylog_head;
        count = KEYLOG_BUF_SIZE;
    } else {
        start = 0;
        count = keylog_head;
    }

    esp_printf(kputc, "=== KEYLOG START ===\n");

    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = (start + i) % KEYLOG_BUF_SIZE;
        char c = keylog_buf[idx];
        if (c == 0 || c == '\r') continue;
        kputc(c);
    }

    esp_printf(kputc, "\n=== KEYLOG END ===\n");
}
