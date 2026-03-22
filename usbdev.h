#pragma once

#include "CH58x_common.h"
#include "fifo8.h"

extern struct fifo8 usbdev_acm_rxfifo;
extern struct fifo8 usbdev_acm_txfifo;
extern struct fifo8 usbdev_hid_rxfifo;
extern struct fifo8 usbdev_hid_txfifo;
extern struct fifo8 usbdev_vendor_rxfifo;
extern struct fifo8 usbdev_vendor_txfifo;

extern void usbdev_init(void);
extern void usbdev_task(void);
extern void usbdev_acm_send(uint8_t *buf, int len);
extern void usbdev_hid_send(uint8_t *buf, int len);
extern void usbdev_vendor_send(uint8_t *buf, int len);
