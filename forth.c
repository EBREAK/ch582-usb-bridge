#include "forth.h"
#include "debug.h"
#include "uart.h"
#include "usbdev.h"
#include "main.h"
#include "config.h"

#include <CH58x_common.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

__aligned(4) uint32_t forth_dp = (uint32_t)&FORTH_RAM_START[0];

struct forth_context *forth_tasks[FORTH_TASK_MAX];

int forth_task_find_by_id(uint8_t taskid)
{
	int ret;
	ret = 0;
	while (ret < FORTH_TASK_MAX) {
		if (forth_tasks[ret]->taskid == taskid) {
			return ret;
		}
		ret += 1;
	}
	return -ENOENT;
}

int forth_task_find_free(void)
{
	int ret;
	ret = 0;
	while (ret < FORTH_TASK_MAX) {
		if (forth_tasks[ret] == NULL) {
			return ret;
		}
		ret += 1;
	}
	return -ENOSPC;
}


__aligned(4) uint16_t FORTH_CONTEXT[FORTH_CONTEXT_SIZE];

void forth_rom_dump(void)
{
	uint32_t idx = 0;
	while (idx < (uint32_t)(FORTH_ROM_END - FORTH_ROM_START)) {
		debug_puthex(FORTH_ROM_START[idx]);
		debug_putc(' ');
		if ((idx % 8) == 7) {
			debug_cr();
		}
		idx += 1;
	}
	debug_cr();
}

__aligned(4) uint32_t stack_root[FORTH_STACK_DEPTH * 2];
uint8_t tib_root[FORTH_NLEN_MASK + 1];
struct forth_context forth_root = {
	.tos = FORTH_TOS_INIT,
	.ip = (uint32_t)&FORTH_SELFTEST[0],
	.rs0 = (uint32_t)&stack_root[(FORTH_STACK_DEPTH * 2)],
	.rsp = (uint32_t)&stack_root[(FORTH_STACK_DEPTH * 2)],
	.ps0 = (uint32_t)&stack_root[(FORTH_STACK_DEPTH * 1)],
	.psp = (uint32_t)&stack_root[(FORTH_STACK_DEPTH * 1)],
	.tib = (uint32_t)&tib_root[0],
	.tin = 0,
};

static inline uint32_t xt2addr(uint16_t xt)
{
	if ((xt & 1) != 0) {
		xt ^= 1;
		return ((uint32_t)&FORTH_RAM_START[0] + (uint32_t)xt);
	}
	return ((uint32_t)&FORTH_ROM_START[0] + (uint32_t)xt);
}

uint16_t Forth_ProcessEvent(uint8_t task_id, uint16_t events)
{
	if (events & SYS_EVENT_MSG) {
		uint8_t *pMsg;

		if ((pMsg = tmos_msg_receive(main_taskid)) != NULL) {
			// Release the TMOS message
			tmos_msg_deallocate(pMsg);
		}
		// return unprocessed events
		return (events ^ SYS_EVENT_MSG);
	}

	if (events & FORTH_EVT_START) {
		tmos_set_event(task_id, FORTH_EVT_RUN);
		return (events ^ FORTH_EVT_START);
	}

	if (events & FORTH_EVT_RUN) {
		int forth_taskidx;
		forth_taskidx = forth_task_find_by_id(task_id);
		if (forth_taskidx >= 0) {
			forth_run(forth_tasks[forth_taskidx]);
		}
		return (events ^ FORTH_EVT_RUN);
	}

	return 0;
}

struct forth_context *forth_task_new(uint32_t ip)
{
	int forth_taskidx;
	forth_taskidx = forth_task_find_free();
	if (forth_taskidx < 0) {
		return NULL;
	}
	struct forth_context *fctx;
	forth_dp += 3;
	forth_dp &= -4;
	fctx = (void *)forth_dp;
	forth_dp += sizeof(struct forth_context);
	memcpy(fctx, &forth_root, sizeof(struct forth_context));
	forth_dp += 3;
	forth_dp &= -4;
	forth_dp += (FORTH_STACK_DEPTH * 4);
	fctx->ps0 = forth_dp;
	fctx->psp = fctx->ps0;
	forth_dp += (FORTH_STACK_DEPTH * 4);
	fctx->rs0 = forth_dp;
	fctx->rsp = fctx->rs0;
	fctx->tos = FORTH_TOS_INIT;
	fctx->sta = 0;
	fctx->ip = ip;
	fctx->taskid = TMOS_ProcessEventRegister(Forth_ProcessEvent);
	forth_tasks[forth_taskidx] = fctx;
	tmos_set_event(fctx->taskid, FORTH_EVT_START);
	return fctx;
}

void forth_init(void)
{
	FORTH_CONTEXT[0] = XT_USER_LATEST;
	FORTH_CONTEXT[1] = XT_FORTH_LATEST;
	forth_root.xt_emit = XT_EARLY_EMIT;
	forth_root.xt_key = XT_EARLY_KEY;
	forth_root.xt_dot = XT_HEXDOT;
	forth_root.taskid = TMOS_ProcessEventRegister(Forth_ProcessEvent);
	forth_tasks[0] = &forth_root;
	tmos_set_event(forth_root.taskid, FORTH_EVT_START);

	debug_puts("FORTH RAM START: ");
	debug_puthex((uint32_t)&FORTH_RAM_START[0]);
	debug_cr();
	debug_puts("FORTH RAM END: ");
	debug_puthex((uint32_t)&FORTH_RAM_END[0]);
	debug_cr();
	debug_puts("FORTH RAM SIZE: ");
	debug_puthex((uint32_t)(FORTH_RAM_END - FORTH_RAM_START));
	debug_cr();
	debug_puts("FORTH ROM START: ");
	debug_puthex((uint32_t)&FORTH_ROM_START[0]);
	debug_cr();
	debug_puts("FORTH ROM END: ");
	debug_puthex((uint32_t)&FORTH_ROM_END[0]);
	debug_cr();
	debug_puts("FORTH ROM SIZE: ");
	debug_puthex((uint32_t)(FORTH_ROM_END - FORTH_ROM_START));
	debug_cr();
	//forth_rom_dump();
}

int tonumber_sigdec(char *start, int count)
{
	if (count == 0) {
		return 0;
	}
	int ret = 0;
	bool negate = false;
	switch (start[0]) {
	case '-':
		start += 1;
		count -= 1;
		negate = true;
		break;
	case '+':
		start += 1;
		count -= 1;
		break;
	}
	while (count > 0) {
		ret = (ret << 3) +
		      (ret << 1); // ret *= 10, can run fast on for rv32ec
		ret += start[0] - '0';
		start += 1;
		count -= 1;
	}
	return negate ? -ret : ret;
}

int tonumber_sighex(char *start, int count)
{
	if (count == 0) {
		return 0;
	}
	int ret = 0;
	bool negate = false;
	switch (start[0]) {
	case '-':
		start += 1;
		count -= 1;
		negate = true;
		break;
	case '+':
		start += 1;
		count -= 1;
		break;
	}
	while (count > 0) {
		ret <<= 4;
		if (start[0] <= '9') {
			ret |= start[0] - '0';
		}
		if (start[0] >= 'A') {
			ret |= start[0] - 'A' + 10;
		}
		start += 1;
		count -= 1;
	}
	return negate ? -ret : ret;
}

static inline uint32_t forth_tonlen(uint16_t xt)
{
	uint32_t ret;
	ret = xt2addr(xt);
	return *(uint8_t *)(ret - 4);
}

static inline uint32_t forth_toname(uint16_t xt)
{
	uint32_t x, ret;
	ret = xt2addr(xt);
	x = *(uint8_t *)(ret - 4);
	x += 1;
	x &= -2;
	ret -= 4;
	ret -= x;
	return ret;
}

static inline uint16_t forth_toprev(uint16_t xt)
{
	return (*(uint16_t *)(xt2addr(xt) - 2));
}

uint16_t forth_wordlist_search(uint32_t addr, uint32_t count, uint16_t xt)
{
	if (xt == 0) {
		return xt;
	}
	if (count == 0) {
		return 0;
	}
	char *name;
	uint32_t nlen;
	do {
		name = (char *)forth_toname(xt);
		nlen = forth_tonlen(xt);
		if ((nlen == count) &&
		    (memcmp(name, (void *)addr, count) == 0)) {
			return xt;
		}
		xt = forth_toprev(xt);
	} while (xt != 0);
	return 0;
}

const char num2hex_lut[] = "0123456789ABCDEF";

void forth_run(struct forth_context *fctx)
{
	if ((fctx == NULL) || ((fctx->sta & FORTH_STA_HALT) != 0)) {
		return;
	}
	if (fctx->save != NULL) {
		goto * fctx->save;
	}
	uint16_t xt;
	uint16_t op;
	uint32_t utmp0, utmp1;
	int32_t stmp0, stmp1;
forth_next:
	xt = *(uint16_t *)fctx->ip;
	fctx->ip += 2;
forth_exec:
	fctx->w = xt2addr(xt);
	op = *(uint16_t *)fctx->w;
	if ((fctx->sta & FORTH_STA_DBG) != 0) {
		debug_puts("FORTH ");
		debug_puthex((uint32_t)fctx);
		debug_puts(" W ");
		debug_puthex(fctx->w);
		debug_puts(" XT ");
		debug_puthex(xt);
		debug_puts(" OP ");
		debug_puthex(op);
		debug_puts(" IP ");
		debug_puthex(fctx->ip);
		debug_puts(" TOS ");
		debug_puthex(fctx->tos);
		debug_puts(" PSP ");
		debug_puthex(fctx->psp);
		debug_puts(" RSP ");
		debug_puthex(fctx->rsp);
		debug_puts(" PS0 ");
		debug_puthex(fctx->ps0);
		debug_puts(" RS0 ");
		debug_puthex(fctx->rs0);
		debug_cr();
	}

	switch (op) {
	case F_NOOP:
		break;
	case F_HALT:
		(void)xt;
forth_halt:
		fctx->sta |= FORTH_STA_HALT;
		debug_puts("FORTH HALT\r\n");
		goto forth_shut;
	case F_SWLIT:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = *(int16_t *)fctx->ip;
		fctx->ip += 2;
		break;
	case F_UWLIT:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = *(uint16_t *)fctx->ip;
		fctx->ip += 2;
		break;
	case F_ULLIT:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = *(uint16_t *)fctx->ip;
		fctx->tos |= (*(uint16_t *)(fctx->ip + 2)) << 16;
		fctx->ip += 4;
		break;
	case F_SWDOCONST:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = *(int16_t *)(fctx->w + 2);
		break;
	case F_UWDOCONST:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = *(uint16_t *)(fctx->w + 2);
		break;
	case F_ULDOCONST:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = *(uint16_t *)(fctx->w + 2);
		fctx->tos |= (*(uint16_t *)(fctx->w + 4)) << 16;
		break;
	case F_BRANCH:
		fctx->ip += *(int16_t *)fctx->ip;
		break;
	case F_ZBRANCH:
		utmp0 = fctx->tos;
		fctx->tos = forth_ppop(fctx);
		if (utmp0 == 0) {
			fctx->ip += *(int16_t *)fctx->ip;
			break;
		}
		fctx->ip += 2;
		break;
	case F_EXIT:
		fctx->ip = forth_rpop(fctx);
		break;
	case F_CALL:
		forth_rpush(fctx, fctx->ip);
		fctx->ip = fctx->w + 2;
		break;
	case F_EXECUTE:
		xt = fctx->tos;
		fctx->tos = forth_ppop(fctx);
		goto forth_exec;
	case F_PERFORM:
		xt = *(uint16_t *)fctx->tos;
		fctx->tos = forth_ppop(fctx);
		goto forth_exec;
	case F_EQCHK:
		utmp0 = fctx->tos;
		utmp1 = forth_ppop(fctx);
		fctx->tos = forth_ppop(fctx);
		if (utmp0 != utmp1) {
			debug_puts("FORTH EQCHK FAIL ");
			debug_puthex(utmp1);
			debug_putc(' ');
			debug_puthex(utmp0);
			debug_cr();
			goto forth_halt;
		}
		break;
	case F_PZCHK:
		if ((fctx->tos != FORTH_TOS_INIT) || (fctx->psp != fctx->ps0)) {
			debug_puts("FORTH PZCHK FAIL\r\n");
			goto forth_halt;
		}
		break;
	case F_DROP:
		fctx->tos = forth_ppop(fctx);
		break;
	case F_DUP:
		forth_ppush(fctx, fctx->tos);
		break;
	case F_SWAP:
		utmp0 = fctx->tos;
		fctx->tos = *(uint32_t *)(fctx->psp + 0);
		*(uint32_t *)(fctx->psp + 0) = utmp0;
		break;
	case F_OVER:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = *(uint32_t *)(fctx->psp + 4);
		break;
	case F_ROT:
		utmp0 = fctx->tos;
		utmp1 = *(uint32_t *)(fctx->psp + 0);
		fctx->tos = *(uint32_t *)(fctx->psp + 4);
		*(uint32_t *)(fctx->psp + 0) = utmp0;
		*(uint32_t *)(fctx->psp + 4) = utmp1;
		break;
	case F_NROT:
		utmp0 = *(uint32_t *)(fctx->psp + 0);
		utmp1 = *(uint32_t *)(fctx->psp + 4);
		*(uint32_t *)(fctx->psp + 0) = utmp1;
		*(uint32_t *)(fctx->psp + 4) = fctx->tos;
		fctx->tos = utmp0;
		break;
	case F_NIP:
		fctx->psp += 4;
		break;
	case F_TOR:
		forth_rpush(fctx, fctx->tos);
		fctx->tos = forth_ppop(fctx);
		break;
	case F_FROMR:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = forth_rpop(fctx);
		break;
	case F_2DROP:
		forth_ppop(fctx);
		fctx->tos = forth_ppop(fctx);
		break;
	case F_2DUP:
		utmp0 = forth_ppop(fctx);
		forth_ppush(fctx, utmp0);
		forth_ppush(fctx, fctx->tos);
		forth_ppush(fctx, utmp0);
		break;
	case F_2SWAP:
		utmp0 = *(uint32_t *)(fctx->psp + 0);
		utmp1 = *(uint32_t *)(fctx->psp + 4);
		*(uint32_t *)(fctx->psp + 4) = fctx->tos;
		fctx->tos = utmp1;
		utmp1 = *(uint32_t *)(fctx->psp + 8);
		*(uint32_t *)(fctx->psp + 0) = utmp1;
		*(uint32_t *)(fctx->psp + 8) = utmp0;
		break;
	case F_2OVER:
		utmp0 = *(uint32_t *)(fctx->psp + 4);
		utmp1 = *(uint32_t *)(fctx->psp + 8);
		fctx->psp -= 8;
		*(uint32_t *)(fctx->psp + 4) = fctx->tos;
		*(uint32_t *)(fctx->psp + 0) = utmp1;
		fctx->tos = utmp0;
		break;
	case F_PLUS:
		utmp0 = forth_ppop(fctx);
		fctx->tos = utmp0 + fctx->tos;
		break;
	case F_1PLUS:
		fctx->tos += 1;
		break;
	case F_2PLUS:
		fctx->tos += 2;
		break;
	case F_4PLUS:
		fctx->tos += 4;
		break;
	case F_MINUS:
		utmp0 = forth_ppop(fctx);
		fctx->tos = utmp0 - fctx->tos;
		break;
	case F_1MINUS:
		fctx->tos -= 1;
		break;
	case F_2MINUS:
		fctx->tos -= 2;
		break;
	case F_4MINUS:
		fctx->tos -= 4;
		break;
	case F_MULTI:
		utmp0 = forth_ppop(fctx);
		fctx->tos = utmp0 * fctx->tos;
		break;
	case F_2MULTI:
		fctx->tos *= 2;
		break;
	case F_4MULTI:
		fctx->tos *= 4;
		break;
	case F_8MULTI:
		fctx->tos *= 8;
		break;
	case F_DIVID:
		utmp0 = forth_ppop(fctx);
		fctx->tos = utmp0 / fctx->tos;
		break;
	case F_2DIVID:
		fctx->tos /= 2;
		break;
	case F_4DIVID:
		fctx->tos /= 4;
		break;
	case F_8DIVID:
		fctx->tos /= 8;
		break;
	case F_LSHIFT:
		utmp0 = forth_ppop(fctx);
		fctx->tos = utmp0 << fctx->tos;
		break;
	case F_RSHIFT:
		utmp0 = forth_ppop(fctx);
		fctx->tos = utmp0 >> fctx->tos;
		break;
	case F_AND:
		utmp0 = forth_ppop(fctx);
		fctx->tos = utmp0 & fctx->tos;
		break;
	case F_OR:
		utmp0 = forth_ppop(fctx);
		fctx->tos = utmp0 | fctx->tos;
		break;
	case F_XOR:
		utmp0 = forth_ppop(fctx);
		fctx->tos = utmp0 ^ fctx->tos;
		break;
	case F_INVERT:
		fctx->tos ^= -1;
		break;
	case F_NEGATE:
		fctx->tos = -fctx->tos;
		break;
	case F_BIC:
		utmp0 = forth_ppop(fctx);
		fctx->tos = utmp0 & (~fctx->tos);
		break;
	case F_EQ:
		utmp0 = fctx->tos;
		utmp1 = forth_ppop(fctx);
		fctx->tos = 0;
		if (utmp1 == utmp0) {
			fctx->tos = -1;
		}
		break;
	case F_NE:
		utmp0 = fctx->tos;
		utmp1 = forth_ppop(fctx);
		fctx->tos = 0;
		if (utmp1 != utmp0) {
			fctx->tos = -1;
		}
		break;
	case F_LT:
		stmp0 = fctx->tos;
		stmp1 = forth_ppop(fctx);
		fctx->tos = 0;
		if (stmp1 < stmp0) {
			fctx->tos = -1;
		}
		break;
	case F_GT:
		stmp0 = fctx->tos;
		stmp1 = forth_ppop(fctx);
		fctx->tos = 0;
		if (stmp1 > stmp0) {
			fctx->tos = -1;
		}
		break;
	case F_EQZ:
		utmp0 = fctx->tos;
		fctx->tos = 0;
		if (utmp0 == 0) {
			fctx->tos = -1;
		}
		break;
	case F_NEZ:
		utmp0 = fctx->tos;
		fctx->tos = 0;
		if (utmp0 != 0) {
			fctx->tos = -1;
		}
		break;
	case F_ULT:
		utmp0 = fctx->tos;
		utmp1 = forth_ppop(fctx);
		fctx->tos = 0;
		if (utmp1 < utmp0) {
			fctx->tos = -1;
		}
		break;
	case F_UGT:
		utmp0 = fctx->tos;
		utmp1 = forth_ppop(fctx);
		fctx->tos = 0;
		if (utmp1 > utmp0) {
			fctx->tos = -1;
		}
		break;
	case F_CLOAD:
		utmp0 = fctx->tos;
		fctx->tos = *(uint8_t *)utmp0;
		break;
	case F_WLOAD:
		utmp0 = fctx->tos;
		if ((utmp0 & 1) == 0) {
			fctx->tos = *(uint16_t *)utmp0;
		} else {
			fctx->tos = *(uint8_t *)utmp0;
			utmp1 = *(uint8_t *)(utmp0 + 1);
			fctx->tos |= utmp1 << 8;
		}
		break;
	case F_LLOAD:
		utmp0 = fctx->tos;
		if ((utmp0 & 3) == 0) {
			fctx->tos = *(uint32_t *)utmp0;
		} else if ((utmp0 & 1) == 0) {
			fctx->tos = *(uint16_t *)utmp0;
			utmp1 = *(uint16_t *)(utmp0 + 2);
			fctx->tos |= utmp1 << 16;
		} else {
			fctx->tos = *(uint8_t *)utmp0;
			utmp1 = *(uint8_t *)(utmp0 + 1);
			fctx->tos |= utmp1 << 8;
			utmp1 = *(uint8_t *)(utmp0 + 2);
			fctx->tos |= utmp1 << 16;
			utmp1 = *(uint8_t *)(utmp0 + 3);
			fctx->tos |= utmp1 << 24;
		}
		break;
	case F_CSTORE:
		utmp0 = fctx->tos;
		utmp1 = forth_ppop(fctx);
		fctx->tos = forth_ppop(fctx);
		*(uint8_t *)utmp0 = utmp1;
		break;
	case F_WSTORE:
		utmp0 = fctx->tos;
		utmp1 = forth_ppop(fctx);
		fctx->tos = forth_ppop(fctx);
		if ((utmp0 & 1) == 0) {
			*(uint16_t *)utmp0 = utmp1;
		} else {
			*(uint8_t *)utmp0 = utmp1;
			*(uint8_t *)(utmp0 + 1) = utmp1 >> 8;
		}
		break;
	case F_LSTORE:
		utmp0 = fctx->tos;
		utmp1 = forth_ppop(fctx);
		fctx->tos = forth_ppop(fctx);
		if ((utmp0 & 3) == 0) {
			*(uint32_t *)utmp0 = utmp1;
		} else if ((utmp0 & 1) == 0) {
			*(uint16_t *)utmp0 = utmp1;
			*(uint16_t *)(utmp0 + 2) = utmp1 >> 16;
		} else {
			*(uint8_t *)utmp0 = utmp1;
			*(uint8_t *)(utmp0 + 1) = utmp1 >> 8;
			*(uint8_t *)(utmp0 + 2) = utmp1 >> 16;
			*(uint8_t *)(utmp0 + 3) = utmp1 >> 24;
		}
		break;
	case F_BLANK:
		utmp0 = fctx->tos;
		utmp1 = forth_ppop(fctx);
		fctx->tos = forth_ppop(fctx);
		bzero((void *)utmp1, utmp0);
		break;
	case F_FILL:
		utmp0 = fctx->tos; // C
		utmp1 = forth_ppop(fctx); // COUNT
		fctx->tos = forth_ppop(fctx); // ADDR
		memset((void *)fctx->tos, utmp0, utmp1);
		fctx->tos = forth_ppop(fctx);
		break;
	case F_CMOVE:
		utmp0 = fctx->tos; // COUNT
		utmp1 = forth_ppop(fctx); // ADDR2
		fctx->tos = forth_ppop(fctx); // ADDR1
		memmove((void *)utmp1, (void *)fctx->tos, utmp0);
		fctx->tos = forth_ppop(fctx);
		break;
	case F_COMPARE:
		stmp0 = fctx->tos; // COUNT2
		utmp0 = forth_ppop(fctx); // ADDR2
		stmp1 = forth_ppop(fctx); // COUNT1
		utmp1 = forth_ppop(fctx); // ADDR1
		fctx->tos = 0;
		if ((stmp0 == 0) && (stmp1 == 0)) {
			break;
		}
		if ((stmp0 == stmp1) && (utmp0 == utmp1)) {
			break;
		}
		if ((stmp0 > stmp1)) {
			fctx->tos = -1;
			break;
		}
		if ((stmp0 < stmp1)) {
			fctx->tos = 1;
			break;
		}
		while (stmp0 > 0) {
			stmp1 = *(uint8_t *)utmp1 - *(uint8_t *)utmp0;
			if (stmp1 < 0) {
				fctx->tos = -1;
				break;
			}
			if (stmp1 > 0) {
				fctx->tos = 1;
				break;
			}
			utmp0 += 1;
			utmp1 += 1;
			stmp0 -= 1;
		}
		break;
	case F_EARLY_EMIT:
forth_wait_early_emit:
		if (R8_UART1_TFC > 0) {
			//debug_puts("NO EMIT");
			fctx->wait_state = FORTH_WAIT_EARLY_EMIT;
			fctx->save = &&forth_wait_early_emit;
			return;
		}
		fctx->wait_state = 0;
		fctx->save = NULL;
		R8_UART1_THR = fctx->tos;
		fctx->tos = forth_ppop(fctx);
		break;
	case F_EARLY_KEY:
forth_wait_early_key:
		if (fifo8_num_used(&uart1_rxfifo) == 0) {
			//debug_puts("NO KEY");
			fctx->wait_state = FORTH_WAIT_EARLY_KEY;
			fctx->save = &&forth_wait_early_key;
			return;
		}
		fctx->wait_state = 0;
		fctx->save = NULL;
		forth_ppush(fctx, fctx->tos);
		fctx->tos = 0;
		fifo8_pop(&uart1_rxfifo, (uint8_t *)&fctx->tos);
		break;
	case F_USER_EMIT:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = (uint32_t)&fctx->xt_emit;
		break;
	case F_USER_KEY:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = (uint32_t)&fctx->xt_key;
		break;
	case F_DBGON:
		fctx->sta |= FORTH_STA_DBG;
		break;
	case F_DBGOFF:
		fctx->sta &= ~FORTH_STA_DBG;
		break;
	case F_NUM2HEX:
		fctx->tos = num2hex_lut[fctx->tos & 0xF];
		break;
	case F_USER_DOT:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = (uint32_t)&fctx->xt_dot;
		break;
	case F_DEPTH:
		utmp0 = (fctx->ps0 - fctx->psp) / 4;
		forth_ppush(fctx, fctx->tos);
		fctx->tos = utmp0;
		break;
	case F_PICK:
		utmp0 = fctx->psp + (fctx->tos * 4);
		fctx->tos = *(uint32_t *)utmp0;
		break;
	case F_ISNUMBER:
		utmp0 = fctx->tos; // COUNT
		utmp1 = forth_ppop(fctx); // ADDR
		fctx->tos = 0;
		if (utmp0 < 2) {
			break;
		}
		switch (*(uint8_t *)utmp1) {
		case '#':
			utmp1 += 1;
			utmp0 -= 1;
			while (utmp0 > 0) {
				if ((!isdigit(*(uint8_t *)utmp1)) &&
				    ((*(uint8_t *)utmp1) != '-') &&
				    ((*(uint8_t *)utmp1) != '+')) {
					break;
				}
				utmp0 -= 1;
				utmp1 += 1;
			}
			if (utmp0 == 0) {
				fctx->tos = -1;
			}
			break;
		case '$':
			utmp1 += 1;
			utmp0 -= 1;
			while (utmp0 > 0) {
				if (!isxdigit(*(uint8_t *)utmp1) &&
				    ((*(uint8_t *)utmp1) != '-') &&
				    ((*(uint8_t *)utmp1) != '+')) {
					break;
				}
				utmp0 -= 1;
				utmp1 += 1;
			}
			if (utmp0 == 0) {
				fctx->tos = -1;
			}
			break;
		default:
			break;
		}
		break;
	case F_TONUMBER:
		utmp0 = fctx->tos; // COUNT
		utmp1 = forth_ppop(fctx); // ADDR
		fctx->tos = 0;
		if (utmp0 < 2) {
			break;
		}
		switch (*(uint8_t *)utmp1) {
		case '#':
			utmp1 += 1;
			utmp0 -= 1;
			fctx->tos = tonumber_sigdec((char *)utmp1, utmp0);
			break;
		case '$':
			utmp1 += 1;
			utmp0 -= 1;
			fctx->tos = tonumber_sighex((char *)utmp1, utmp0);
			break;
		default:
			break;
		}
		break;
	case F_WALIGNED:
		fctx->tos += 1;
		fctx->tos &= -2;
		break;
	case F_LALIGNED:
		fctx->tos += 3;
		fctx->tos &= -4;
		break;
	case F_XALIGNED:
		fctx->tos += 7;
		fctx->tos &= -8;
		break;
	case F_TONLEN:
		fctx->tos = forth_tonlen(fctx->tos);
		break;
	case F_TONAME:
		fctx->tos = forth_toname(fctx->tos);
		break;
	case F_TOPREV:
		fctx->tos = forth_toprev(fctx->tos);
		break;
	case F_CONTEXT:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = (uint32_t)&FORTH_CONTEXT[0];
		break;
	case F_CONTEXT_SIZE:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = FORTH_CONTEXT_SIZE;
		break;
	case F_WORDLIST_SEARCH:
		utmp0 = forth_ppop(fctx); // COUNT
		utmp1 = forth_ppop(fctx); // ADDR
		fctx->tos = forth_wordlist_search(utmp1, utmp0, fctx->tos);
		break;
	case F_ISDELIM:
		utmp0 = fctx->tos;
		fctx->tos = 0;
		if ((utmp0 == '\n') || (utmp0 == '\r') || (utmp0 == ' ')) {
			fctx->tos = -1;
		}
		break;
	case F_TIB:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = fctx->tib;
		break;
	case F_TINLOAD:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = fctx->tin;
		break;
	case F_TINSTORE:
		fctx->tin = fctx->tos;
		fctx->tos = forth_ppop(fctx);
		break;
	case F_STATELOAD:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = 0;
		if ((fctx->sta & FORTH_STA_COMPILE) != 0) {
			fctx->tos = -1;
		}
		break;
	case F_COMPOFF:
		fctx->sta &= ~(FORTH_STA_COMPILE);
		break;
	case F_COMPON:
		fctx->sta |= FORTH_STA_COMPILE;
		break;
	case F_ISIMMEDIATE:
		utmp0 = fctx->tos;
		fctx->tos = 0;
		if (((*(uint8_t *)(xt2addr(utmp0) - 3)) & (1 << 7)) != 0) {
			fctx->tos = -1;
		}
		break;
	case F_PSTACK_RST:
		fctx->psp = fctx->ps0;
		fctx->tos = FORTH_TOS_INIT;
		break;
	case F_ABS:
		fctx->tos = abs((int32_t)fctx->tos);
		break;
	case F_XT2ADDR:
		fctx->tos = xt2addr(fctx->tos);
		break;
	case F_XT2ENTR:
		fctx->tos = xt2addr(fctx->tos);
		fctx->tos = *(uint16_t *)(fctx->tos + 0);
		break;
	case F_BLENLOAD:
		fctx->tos = *(uint8_t *)(xt2addr(fctx->tos) - 3);
		fctx->tos &= 0x7F;
		fctx->tos <<= 1;
		break;
	case F_BLENSTORE:
		utmp0 = forth_ppop(fctx);
		*(uint8_t *)(xt2addr(fctx->tos) - 3) = (utmp0 & 0x7F) >> 1;
		fctx->tos = forth_ppop(fctx);
		break;
	case F_ACM0_EMIT:
forth_wait_acm0_emit:
		if (fifo8_num_free(&usbdev_acm_0_d2h_fifo) == 0) {
			//debug_puts("NO EMIT");
			fctx->wait_state = FORTH_WAIT_ACM0_EMIT;
			fctx->save = &&forth_wait_acm0_emit;
			return;
		}
		fctx->wait_state = 0;
		fctx->save = NULL;
		fifo8_push(&usbdev_acm_0_d2h_fifo, fctx->tos);
		fctx->tos = forth_ppop(fctx);
		break;
	case F_ACM0_KEY:
forth_wait_acm0_key:
		if (fifo8_num_used(&usbdev_acm_0_h2d_fifo) == 0) {
			//debug_puts("NO KEY");
			fctx->wait_state = FORTH_WAIT_ACM0_KEY;
			fctx->save = &&forth_wait_acm0_key;
			return;
		}
		fctx->wait_state = 0;
		fctx->save = NULL;
		forth_ppush(fctx, fctx->tos);
		fctx->tos = 0;
		fifo8_pop(&usbdev_acm_0_h2d_fifo, (uint8_t *)&fctx->tos);
		break;
	case F_ACM1_EMIT:
forth_wait_acm1_emit:
		if (fifo8_num_free(&usbdev_acm_1_d2h_fifo) == 0) {
			//debug_puts("NO EMIT");
			fctx->wait_state = FORTH_WAIT_ACM1_EMIT;
			fctx->save = &&forth_wait_acm1_emit;
			return;
		}
		fctx->wait_state = 0;
		fctx->save = NULL;
		fifo8_push(&usbdev_acm_1_d2h_fifo, fctx->tos);
		fctx->tos = forth_ppop(fctx);
		break;
	case F_ACM1_KEY:
forth_wait_acm1_key:
		if (fifo8_num_used(&usbdev_acm_1_h2d_fifo) == 0) {
			//debug_puts("NO KEY");
			fctx->wait_state = FORTH_WAIT_ACM1_KEY;
			fctx->save = &&forth_wait_acm1_key;
			return;
		}
		fctx->wait_state = 0;
		fctx->save = NULL;
		forth_ppush(fctx, fctx->tos);
		fctx->tos = 0;
		fifo8_pop(&usbdev_acm_1_h2d_fifo, (uint8_t *)&fctx->tos);
		break;
	case F_SW_RESET:
		PFIC_SystemReset();
		while (1)
			;
		break;
	case F_PW_RESET:
		FLASH_ROM_SW_RESET();
		sys_safe_access_enable();
		R16_INT32K_TUNE = 0xFFFF;
		sys_safe_access_disable();
		sys_safe_access_enable();
		R8_RST_WDOG_CTRL |= RB_SOFTWARE_RESET;
		sys_safe_access_disable();
		while (1)
			;
		break;
	case F_TICK_DELAY:
		utmp0 = fctx->tos;
		tmos_start_task(fctx->taskid, FORTH_EVT_RUN, utmp0);
		fctx->save = &&forth_wait_tick_delay;
		fctx->wait_state = FORTH_WAIT_TICK_DELAY;
		return;
forth_wait_tick_delay:
		fctx->tos = forth_ppop(fctx);
		fctx->save = NULL;
		fctx->wait_state = 0;
		break;
	case F_TICK_COUNT:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = TMOS_GetSystemClock();
		break;
	case F_DP:
		forth_ppush(fctx, fctx->tos);
		fctx->tos = (uint32_t)&forth_dp;
		break;
	case F_TOBODY:
		fctx->tos = xt2addr(fctx->tos);
		fctx->tos += 2;
		break;
	case F_TASK_NEW:
		fctx->tos = (uint32_t)forth_task_new(fctx->tos);
		break;
	default:
		debug_puts("INVALID OPCODE\r\n");
		goto forth_halt;
	}
	goto forth_next;
forth_shut:
	return;
}
