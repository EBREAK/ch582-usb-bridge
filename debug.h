#pragma once

#include <stdint.h>
#include <stdbool.h>

extern void debug_putc(int c);
extern void debug_puts(char *s);
extern void debug_putnhex(int num, int width);
extern void debug_assert(char *msg, int line, bool expr);

#define debug_cr() debug_puts("\r\n")
#define debug_puthex(x) debug_putnhex((x), sizeof(x) * 2)
