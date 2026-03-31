#include "main.h"
#include "uart.h"
#include "fifo8.h"
#include "forth.h"
#include "config.h"

static int uart1_trig_bytes = 4;

__aligned(4) uint8_t uart1_rxfifo_buf[128];
struct fifo8 uart1_rxfifo = {
	.buf = &uart1_rxfifo_buf[0],
	.mask = 128 - 1,
	.head = 0,
	.tail = 0,
};


void uart_task(void)
{
	if (fifo8_num_used(&uart1_rxfifo) > 0) {
		if (forth_root.wait_state == FORTH_WAIT_EARLY_KEY) {
			tmos_set_event(main_taskid, MAIN_EVT_FORTH);
		}
	}
	if (R8_UART1_TFC == 0) {
		if (forth_root.wait_state == FORTH_WAIT_EARLY_EMIT) {
			tmos_set_event(main_taskid, MAIN_EVT_FORTH);
		}
	}
}

void uart_init(void)
{
	fifo8_reset(&uart1_rxfifo);
	while ((R8_UART1_LSR & RB_LSR_TX_ALL_EMP) == 0) {
		__nop();
	}
	GPIOA_SetBits(bTXD1);
	GPIOA_ModeCfg(bTXD1, GPIO_ModeOut_PP_5mA);
	UART1_DefInit();
	UART1_ByteTrigCfg(UART_4BYTE_TRIG);
	UART1_INTCfg(ENABLE, RB_IER_RECV_RDY);
	PFIC_EnableIRQ(UART1_IRQn);
}

__INTERRUPT
__HIGH_CODE
void UART1_IRQHandler(void)
{
	switch (UART1_GetITFlag()) {
	case UART_II_LINE_STAT:
		UART1_GetLinSTA();
		break;
	case UART_II_RECV_RDY:
		for (int i = 0; i != uart1_trig_bytes; i++) {
			fifo8_push(&uart1_rxfifo, UART1_RecvByte());
		}
		break;
	case UART_II_RECV_TOUT:
		while (R8_UART1_RFC) {
			fifo8_push(&uart1_rxfifo, R8_UART1_RBR);
		}
		break;
	case UART_II_THR_EMPTY:
		break;
	case UART_II_MODEM_CHG:
		break;
	default:
		break;
	}
}
