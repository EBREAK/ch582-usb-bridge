#pragma once

#include "fifo8.h"

extern struct fifo8 uart0_rxfifo;
extern struct fifo8 uart1_rxfifo;

extern void uart_task(void);
extern void uart_init(void);
