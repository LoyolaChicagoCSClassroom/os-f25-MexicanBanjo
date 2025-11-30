#ifndef PORTIO_H
#define PORTIO_H

#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint8_t value, uint16_t port) {
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

#endif  // PORTIO_H
