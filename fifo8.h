#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

struct fifo8 {
	volatile uint8_t *buf;
	volatile uint32_t mask;
	volatile uint32_t head;
	volatile uint32_t tail;
};

static inline void fifo8_reset(struct fifo8 *fifo)
{
	fifo->head = 0;
	fifo->tail = 0;
	atomic_thread_fence(memory_order_seq_cst);
}

static inline bool fifo8_push(struct fifo8 *fifo, uint8_t c)
{
	uint32_t current_tail = fifo->tail;
	uint32_t next_tail = (current_tail + 1) & fifo->mask;
	if (next_tail == fifo->head) {
		return false;
	}
	fifo->buf[current_tail] = c;
	atomic_thread_fence(memory_order_seq_cst);
	fifo->tail = next_tail;
	return true;
}

static inline bool fifo8_pop(struct fifo8 *fifo, uint8_t *c)
{
	uint32_t current_head = fifo->head;
	if (current_head == fifo->tail) {
		return false;
	}
	*c = fifo->buf[current_head];
	atomic_thread_fence(memory_order_seq_cst);
	fifo->head = (current_head + 1) & fifo->mask;
	return true;
}
