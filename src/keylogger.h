#ifndef KEYLOGGER_H
#define KEYLOGGER_H

#include <stdint.h>

// Initialize/reset the buffer (optional but nice to have)
void keylog_init(void);

// Log a single character (printable or not – you can filter later)
void keylog_add_char(char c);

// Dump the contents of the log to the screen
void keylog_dump(void);

#endif
