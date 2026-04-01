#include "main.h"
#include "uart.h"
#include "fifo8.h"
#include "forth.h"
#include "usbdev.h"
#include "debug.h"
#include "config.h"


static int uart1_trig_bytes = 4;

__aligned(4) uint8_t uart1_rxfifo_buf[128];
struct fifo8 uart1_rxfifo = {
	.buf = &uart1_rxfifo_buf[0],
	.mask = 128 - 1,
	.head = 0,
	.tail = 0,
};

static int uart0_trig_bytes = 4;

__aligned(4) uint8_t uart0_rxfifo_buf[1024];
struct fifo8 uart0_rxfifo = {
	.buf = &uart0_rxfifo_buf[0],
	.mask = 1024 - 1,
	.head = 0,
	.tail = 0,
};

void uart0_task(void)
{
	uint8_t c = 0;
	if (usbdev_acm_1_mode == USBDEV_ACM1_MODE_UART0) {
		while ((R8_UART0_TFC < 4) &&
		    (fifo8_num_used(&usbdev_acm_1_h2d_fifo) > 0)) {
			fifo8_pop(&usbdev_acm_1_h2d_fifo, &c);
			R8_UART0_THR = c;
		}
		while ((fifo8_num_free(&usbdev_acm_1_d2h_fifo) > 0) &&
		       (fifo8_num_used(&uart0_rxfifo) > 0)) {
			fifo8_pop(&uart0_rxfifo, &c);
			fifo8_push(&usbdev_acm_1_d2h_fifo, c);
		}
		return;
	}
}

void uart1_task(void)
{
	if (R8_UART1_TFC == 0) {
		if (forth_root.wait_state == FORTH_WAIT_EARLY_EMIT) {
			tmos_set_event(main_taskid, MAIN_EVT_FORTH);
		}
	}
	if (fifo8_num_used(&uart1_rxfifo) > 0) {
		if (forth_root.wait_state == FORTH_WAIT_EARLY_KEY) {
			tmos_set_event(main_taskid, MAIN_EVT_FORTH);
		}
	}
}

void uart_task(void)
{
	uart0_task();
	uart1_task();
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

	/*
	  DEFAULT UART0 PIN:

	  TXD: PB7
	  RXD: PB4
	  RTS: PB6
	  CTS: PB0
	 */
	fifo8_reset(&uart0_rxfifo);
	GPIOB_SetBits(bTXD0);
	GPIOB_ModeCfg(bTXD0, GPIO_ModeOut_PP_5mA);
	GPIOB_ModeCfg(bRXD0, GPIO_ModeIN_PU);
	GPIOB_ModeCfg(bRTS, GPIO_ModeOut_PP_5mA);
	GPIOB_ModeCfg(bCTS, GPIO_ModeIN_PD);
	UART0_BaudRateCfg(1200);
	R8_UART0_MCR |= RB_MCR_AU_FLOW_EN;
	R8_UART0_FCR = (2 << 6) | RB_FCR_TX_FIFO_CLR | RB_FCR_RX_FIFO_CLR |
		       RB_FCR_FIFO_EN; // FIFO打开，触发点4字节
	R8_UART0_LCR = RB_LCR_WORD_SZ;
	R8_UART0_IER = RB_IER_TXD_EN | RB_IER_RTS_EN;
	R8_UART0_DIV = 1;

	UART0_ByteTrigCfg(UART_4BYTE_TRIG);
	UART0_INTCfg(ENABLE, RB_IER_RECV_RDY);
	PFIC_EnableIRQ(UART0_IRQn);
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

__INTERRUPT
__HIGH_CODE
void UART0_IRQHandler(void)
{
	switch (UART0_GetITFlag()) {
	case UART_II_LINE_STAT:
		UART0_GetLinSTA();
		break;
	case UART_II_RECV_RDY:
		for (int i = 0; i != uart0_trig_bytes; i++) {
			fifo8_push(&uart0_rxfifo, UART0_RecvByte());
		}
		break;
	case UART_II_RECV_TOUT:
		while (R8_UART0_RFC) {
			fifo8_push(&uart0_rxfifo, R8_UART0_RBR);
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
