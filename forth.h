#pragma once

#include <stdint.h>
#include <stddef.h>

#include "FORTH_DEFS.H"

#define FORTH_EVT_START (1 << 0)
#define FORTH_EVT_RUN (1 << 1)

#define FORTH_STA_HALT (1 << 31)
#define FORTH_STA_DBG (1 << 30)
#define FORTH_STA_COMPILE (1 << 29)

struct forth_context {
	uint32_t ip;
	uint32_t w;
	uint32_t tos;
	uint32_t psp;
	uint32_t ps0;
	uint32_t rsp;
	uint32_t rs0;
	volatile uint32_t sta;
	uint32_t tib;
	uint8_t tin;
	uint16_t xt_emit;
	uint16_t xt_key;
	uint16_t xt_dot;

	uint16_t wait_state;
	void *save;
	uint8_t taskid;
};

enum {
	FORTH_WAIT_EARLY_EMIT = 1,
	FORTH_WAIT_EARLY_KEY = 2,
	FORTH_WAIT_ACM0_EMIT = 3,
	FORTH_WAIT_ACM0_KEY = 4,
	FORTH_WAIT_ACM1_EMIT = 5,
	FORTH_WAIT_ACM1_KEY = 6,
	FORTH_WAIT_TICK_DELAY = 7,
};

static inline uint32_t forth_ppop(struct forth_context *fctx)
{
	uint32_t ret;
	ret = *(uint32_t *)fctx->psp;
	fctx->psp += 4;
	return ret;
}

static inline void forth_ppush(struct forth_context *fctx, uint32_t v)
{
	fctx->psp -= 4;
	*(uint32_t *)fctx->psp = v;
}

static inline uint32_t forth_rpop(struct forth_context *fctx)
{
	uint32_t ret;
	ret = *(uint32_t *)fctx->rsp;
	fctx->rsp += 4;
	return ret;
}

static inline void forth_rpush(struct forth_context *fctx, uint32_t v)
{
	fctx->rsp -= 4;
	*(uint32_t *)fctx->rsp = v;
}

extern struct forth_context *forth_tasks[FORTH_TASK_MAX];
extern struct forth_context forth_root;
extern void forth_init(void);
extern void forth_run(struct forth_context *fctx);
extern const uint8_t FORTH_ROM_START[];
extern const uint8_t FORTH_ROM_END[];
extern const uint8_t FORTH_RAM_START[];
extern const uint8_t FORTH_RAM_END[];
extern const uint8_t FORTH_SELFTEST[];
extern const uint16_t XT_EARLY_EMIT;
extern const uint16_t XT_EARLY_KEY;
extern const uint16_t XT_HEXDOT;
extern const uint16_t XT_FORTH_LATEST;
extern const uint16_t XT_USER_LATEST;
