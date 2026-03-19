#include "CH58x_common.h"
#include "debug.h"

void debug_putc(int c)
{
	UART1_SendString((uint8_t *)&c, 1);
}

void debug_puts(char *s)
{
	UART1_SendString((uint8_t *)s, strlen(s));
}

void debug_putnhex(int num, int width)
{
	const char num2hex_lut[] = "0123456789ABCDEF";
	while (width > 0) {
		debug_putc(num2hex_lut[(num >> ((width - 1) * 4)) & 0xF]);
		width -= 1;
	}
}

void debug_assert(char *msg, int line, bool expr)
{
	if (!expr) {
		debug_puts("ASSERT FAILED ");
		debug_puts(msg);
		debug_putc(' ');
		debug_puthex(line);
		debug_cr();
		while(1);
	}
	return;
}
