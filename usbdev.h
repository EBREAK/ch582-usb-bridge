#pragma once

#include "CH58x_common.h"
#include "fifo8.h"

extern void usbdev_init(void);
extern void usbdev_task(void);
extern void usbdev_show_stat(void);

extern struct fifo8 usbdev_acm_0_h2d_fifo;
extern struct fifo8 usbdev_acm_0_d2h_fifo;
extern struct fifo8 usbdev_acm_1_h2d_fifo;
extern struct fifo8 usbdev_acm_1_d2h_fifo;
extern volatile uint32_t usbdev_acm_1_mode;

enum {
	USBDEV_ACM1_MODE_FORTH = 0,
	USBDEV_ACM1_MODE_UART0 = 1,
};
