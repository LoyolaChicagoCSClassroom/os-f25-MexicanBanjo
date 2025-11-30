#include <stdint.h>
#include "serial.h"
#include "interrupt.h"

#define COM1 0x3F8

static int serial_is_transmit_empty() {
    return inb(COM1 + 5) & 0x20;
}

void serial_write(char c) {
    while (!serial_is_transmit_empty());
    outb(COM1, c);
}
