#include "usbdev.h"
#include "config.h"
#include "debug.h"
#include "main.h"
#include "forth.h"

/*
  CDC ACM 0
  NOTIFACTION EP IN 0X83
  DATA EP IN 0X81
  DATA EP OUT 0X02

  CDC ACM 1
  NOTIFACTION EP IN 0X87
  DATA EP IN 0X85
  DATA EP OUT 0X06
 */

// 线路编码结构体，符合CDC规范
struct cdc_line_coding {
	uint32_t dwDTERate; // 波特率
	uint8_t bCharFormat; // 停止位：0-1位，1-1.5位，2-2位
	uint8_t bParityType; // 校验：0-无，1-奇，2-偶，3-标记，4-空间
	uint8_t bDataBits; // 数据位：5,6,7,8,16
} __attribute__((packed));

// 为两个CDC ACM接口分别存储线路编码参数
static struct cdc_line_coding usbdev_acm_0_line_coding;
static struct cdc_line_coding usbdev_acm_1_line_coding;

// 用于标记SET_LINE_CODING请求的待处理接口（0xFF表示无待处理）
volatile uint8_t usbdev_set_line_coding_pending = 0xFF;

volatile uint32_t usbdev_acm_1_mode = USBDEV_ACM1_MODE_UART0;

volatile bool cdc_acm_0_h2d_pause = false;
volatile uint32_t cdc_acm_0_h2d_total = 0;
volatile uint32_t cdc_acm_0_d2h_total = 0;

volatile bool cdc_acm_1_h2d_pause = false;
volatile uint32_t cdc_acm_1_h2d_total = 0;
volatile uint32_t cdc_acm_1_d2h_total = 0;

#define USBDEV_ACM_0_H2D_BUFSIZE (0x1F + 1)

__aligned(4) uint8_t usbdev_acm_0_h2d_fifo_buf[USBDEV_ACM_0_H2D_BUFSIZE];
struct fifo8 usbdev_acm_0_h2d_fifo = {
	.buf = &usbdev_acm_0_h2d_fifo_buf[0],
	.mask = USBDEV_ACM_0_H2D_BUFSIZE - 1,
	.head = 0,
	.tail = 0,
};

#define USBDEV_ACM_0_D2H_BUFSIZE (0x1F + 1)

__aligned(4) uint8_t usbdev_acm_0_d2h_fifo_buf[USBDEV_ACM_0_D2H_BUFSIZE];
struct fifo8 usbdev_acm_0_d2h_fifo = {
	.buf = &usbdev_acm_0_d2h_fifo_buf[0],
	.mask = USBDEV_ACM_0_D2H_BUFSIZE - 1,
	.head = 0,
	.tail = 0,
};

#define USBDEV_ACM_1_H2D_BUFSIZE (0xFF + 1)

__aligned(4) uint8_t usbdev_acm_1_h2d_fifo_buf[USBDEV_ACM_1_H2D_BUFSIZE];
struct fifo8 usbdev_acm_1_h2d_fifo = {
	.buf = &usbdev_acm_1_h2d_fifo_buf[0],
	.mask = USBDEV_ACM_1_H2D_BUFSIZE - 1,
	.head = 0,
	.tail = 0,
};

#define USBDEV_ACM_1_D2H_BUFSIZE (0x7F + 1)

__aligned(4) uint8_t usbdev_acm_1_d2h_fifo_buf[USBDEV_ACM_1_D2H_BUFSIZE];
struct fifo8 usbdev_acm_1_d2h_fifo = {
	.buf = &usbdev_acm_1_d2h_fifo_buf[0],
	.mask = USBDEV_ACM_1_D2H_BUFSIZE - 1,
	.head = 0,
	.tail = 0,
};

void usbdev_show_stat(void)
{
	static uint32_t prev_time;
	if ((TMOS_GetSystemClock() - prev_time) < 1600) {
		return;
	}
	debug_puts("USBDEV CDC ACM 0 H2D TOTAL ");
	debug_puthex(cdc_acm_0_h2d_total);
	debug_puts(" D2H TOTAL ");
	debug_puthex(cdc_acm_0_d2h_total);
	debug_cr();
	debug_puts("USBDEV CDC ACM 1 H2D TOTAL ");
	debug_puthex(cdc_acm_1_h2d_total);
	debug_puts(" D2H TOTAL ");
	debug_puthex(cdc_acm_1_d2h_total);
	debug_cr();
	prev_time = TMOS_GetSystemClock();
}

#define EP5_GetINSta() (R8_UEP5_CTRL & UEP_T_RES_NAK)
void DevEP5_IN_Deal(uint8_t l)
{
	R8_UEP5_T_LEN = l;
	R8_UEP5_CTRL = (R8_UEP5_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
}

void DevEP6_OUT_Deal(uint8_t len);

#define EP7_GetINSta() (R8_UEP7_CTRL & UEP_T_RES_NAK)
void DevEP7_IN_Deal(uint8_t l)
{
	R8_UEP7_T_LEN = l;
	R8_UEP7_CTRL = (R8_UEP7_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
}

#define DevEP0SIZE 0x40

// 支持的最大接口数量
#define USB_INTERFACE_MAX_NUM 4
// 接口号的最大值
#define USB_INTERFACE_MAX_INDEX 3

// 设备描述符
const uint8_t MyDevDescr[] = {
	// GENERATE BY DEEPSEEK
	// Device Descriptor (18 bytes)
	0x12, // bLength
	0x01, // bDescriptorType (Device)
	0x10, 0x01, // bcdUSB = 1.10
	0xEF, // bDeviceClass (Miscellaneous)
	0x02, // bDeviceSubClass (Common Class)
	0x01, // bDeviceProtocol (IAD)
	0x40, // bMaxPacketSize0 (64)
	0xAB, 0x89, // idVendor  (0x89AB)
	0xCD, 0xEF, // idProduct (0xCDEF)
	0x00, 0x01, // bcdDevice = 1.00
	0x01, // iManufacturer
	0x02, // iProduct
	0x00, // iSerialNumber
	0x01 // bNumConfigurations
};
// 配置描述符
const uint8_t MyCfgDescr[] = {
	// GENERATE BY DEEPSEEK
	// ========== Configuration Descriptor (9 bytes) ==========
	0x09, // bLength: 描述符长度（9字节）
	0x02, // bDescriptorType: CONFIGURATION (0x02)
	0x8D,
	0x00, // wTotalLength: 配置描述符集合总长度 = 130 字节
	0x04, // bNumInterfaces: 4个接口（0~3）
	0x01, // bConfigurationValue: 配置编号（1）
	0x00, // iConfiguration: 字符串描述符索引（无）
	0x80, // bmAttributes: 总线供电 (0x80)，无远程唤醒
	0x32, // bMaxPower: 最大功耗 = 100 mA (0x32 * 2 mA)

	// ========== IAD for CDC ACM (interfaces 0 & 1) ==========
	// 接口关联描述符：将接口0（通信类）和接口1（数据类）关联为CDC ACM设备
	0x08, // bLength: 8字节
	0x0B, // bDescriptorType: INTERFACE_ASSOCIATION (0x0B)
	0x00, // bFirstInterface: 起始接口号 = 0
	0x02, // bInterfaceCount: 关联的接口数 = 2
	0x02, // bFunctionClass: 功能类 = 2 (CDC Communication)
	0x02, // bFunctionSubClass: 子类 = 2 (ACM)
	0x01, // bFunctionProtocol: 协议 = 1 (AT commands)
	0x00, // iFunction: 字符串索引（无）

	// ========== CDC Communication Interface (Interface 0) ==========
	// 接口描述符：CDC 通信类接口（控制接口）
	0x09, // bLength: 9字节
	0x04, // bDescriptorType: INTERFACE (0x04)
	0x00, // bInterfaceNumber: 接口号 = 0
	0x00, // bAlternateSetting: 备用设置 = 0
	0x01, // bNumEndpoints: 使用的端点数（1个通知端点）
	0x02, // bInterfaceClass: 类 = 2 (CDC Communication)
	0x02, // bInterfaceSubClass: 子类 = 2 (ACM)
	0x01, // bInterfaceProtocol: 协议 = 1 (AT commands)
	0x00, // iInterface: 字符串索引（无）

	// ---- CDC Functional Descriptors (CS_INTERFACE) ----
	// Header Functional Descriptor
	0x05, // bLength: 5字节
	0x24, // bDescriptorType: CS_INTERFACE (0x24)
	0x00, // bDescriptorSubtype: HEADER (0x00)
	0x10,
	0x01, // bcdCDC: CDC规范版本 = 1.10 (0x0110)
	// ACM Functional Descriptor
	0x04, // bLength: 4字节
	0x24, // bDescriptorType: CS_INTERFACE (0x24)
	0x02, // bDescriptorSubtype: ACM (0x02)
	0x02, // bmCapabilities: 支持设置线路编码、获取线路状态、发送控制信号
	// Union Functional Descriptor
	0x05, // bLength: 5字节
	0x24, // bDescriptorType: CS_INTERFACE (0x24)
	0x06, // bDescriptorSubtype: UNION (0x06)
	0x00, // bMasterInterface: 主接口 = 0（通信接口）
	0x01, // bSlaveInterface0: 从接口 = 1（数据接口）
	// Call Management Functional Descriptor
	0x05, // bLength: 5字节
	0x24, // bDescriptorType: CS_INTERFACE (0x24)
	0x01, // bDescriptorSubtype: CALL_MANAGEMENT (0x01)
	0x01, // bmCapabilities: 设备处理呼叫管理
	0x01, // bDataInterface: 数据接口编号 = 1

	// ---- Notification Endpoint (0x83 IN) ----
	0x07, // bLength: 7字节
	0x05, // bDescriptorType: ENDPOINT (0x05)
	0x83, // bEndpointAddress: IN端点 0x83
	0x03, // bmAttributes: 中断端点
	0x10,
	0x00, // wMaxPacketSize: 16字节
	0x01, // bInterval: 轮询间隔 = 1 ms

	// ========== CDC Data Interface (Interface 1) ==========
	// 数据接口描述符
	0x09, // bLength: 9字节
	0x04, // bDescriptorType: INTERFACE (0x04)
	0x01, // bInterfaceNumber: 接口号 = 1
	0x00, // bAlternateSetting: 备用设置 = 0
	0x02, // bNumEndpoints: 2个端点（IN/OUT）
	0x0A, // bInterfaceClass: 类 = 0x0A (CDC Data)
	0x00, // bInterfaceSubClass: 子类 = 0
	0x00, // bInterfaceProtocol: 协议 = 0
	0x00, // iInterface: 字符串索引（无）

	// ---- Bulk Endpoints ----
	0x07, // bLength: 7字节
	0x05, // bDescriptorType: ENDPOINT (0x05)
	0x81, // bEndpointAddress: IN端点 0x81
	0x02, // bmAttributes: 批量端点
	0x08,
	0x00, // wMaxPacketSize: 8字节 (全速模式)
	0x00, // bInterval: 忽略（批量端点）

	0x07, // bLength: 7字节
	0x05, // bDescriptorType: ENDPOINT (0x05)
	0x02, // bEndpointAddress: OUT端点 0x02
	0x02, // bmAttributes: 批量端点
	0x08,
	0x00, // wMaxPacketSize: 8字节
	0x00, // bInterval: 忽略

	// ========== IAD for Second CDC ACM (interfaces 2 & 3) ==========
	0x08, // bLength: 8字节
	0x0B, // bDescriptorType: INTERFACE_ASSOCIATION (0x0B)
	0x02, // bFirstInterface: 起始接口号 = 2
	0x02, // bInterfaceCount: 关联的接口数 = 2
	0x02, // bFunctionClass: 功能类 = 2 (CDC Communication)
	0x02, // bFunctionSubClass: 子类 = 2 (ACM)
	0x01, // bFunctionProtocol: 协议 = 1 (AT commands)
	0x00, // iFunction: 字符串索引 (无)

	// ========== CDC Communication Interface (Interface 2) ==========
	0x09, // bLength: 9字节
	0x04, // bDescriptorType: INTERFACE (0x04)
	0x02, // bInterfaceNumber: 接口号 = 2
	0x00, // bAlternateSetting: 备用设置 = 0
	0x01, // bNumEndpoints: 使用的端点数（1个通知端点）
	0x02, // bInterfaceClass: 类 = 2 (CDC Communication)
	0x02, // bInterfaceSubClass: 子类 = 2 (ACM)
	0x01, // bInterfaceProtocol: 协议 = 1 (AT commands)
	0x00, // iInterface: 字符串索引 (无)

	// ---- CDC Functional Descriptors (CS_INTERFACE) ----
	0x05, // bLength: 5字节
	0x24, // bDescriptorType: CS_INTERFACE (0x24)
	0x00, // bDescriptorSubtype: HEADER (0x00)
	0x10,
	0x01, // bcdCDC: CDC规范版本 = 1.10 (0x0110)

	0x04, // bLength: 4字节
	0x24, // bDescriptorType: CS_INTERFACE (0x24)
	0x02, // bDescriptorSubtype: ACM (0x02)
	0x02, // bmCapabilities: 支持设置线路编码、获取线路状态、发送控制信号

	0x05, // bLength: 5字节
	0x24, // bDescriptorType: CS_INTERFACE (0x24)
	0x06, // bDescriptorSubtype: UNION (0x06)
	0x02, // bMasterInterface: 主接口 = 2（通信接口）
	0x03, // bSlaveInterface0: 从接口 = 3（数据接口）

	0x05, // bLength: 5字节
	0x24, // bDescriptorType: CS_INTERFACE (0x24)
	0x01, // bDescriptorSubtype: CALL_MANAGEMENT (0x01)
	0x01, // bmCapabilities: 设备处理呼叫管理
	0x03, // bDataInterface: 数据接口编号 = 3

	// ---- Notification Endpoint (0x87 IN) ----
	0x07, // bLength: 7字节
	0x05, // bDescriptorType: ENDPOINT (0x05)
	0x87, // bEndpointAddress: IN端点 0x87
	0x03, // bmAttributes: 中断端点
	0x10,
	0x00, // wMaxPacketSize: 16字节
	0x01, // bInterval: 轮询间隔 = 1 ms

	// ========== CDC Data Interface (Interface 3) ==========
	0x09, // bLength: 9字节
	0x04, // bDescriptorType: INTERFACE (0x04)
	0x03, // bInterfaceNumber: 接口号 = 3
	0x00, // bAlternateSetting: 备用设置 = 0
	0x02, // bNumEndpoints: 2个端点（IN/OUT）
	0x0A, // bInterfaceClass: 类 = 0x0A (CDC Data)
	0x00, // bInterfaceSubClass: 子类 = 0
	0x00, // bInterfaceProtocol: 协议 = 0
	0x00, // iInterface: 字符串索引 (无)

	// ---- Bulk Endpoints ----
	0x07, // bLength: 7字节
	0x05, // bDescriptorType: ENDPOINT (0x05)
	0x85, // bEndpointAddress: IN端点 0x85
	0x02, // bmAttributes: 批量端点
	0x40,
	0x00, // wMaxPacketSize: 64字节 (全速模式)
	0x00, // bInterval: 忽略（批量端点）

	0x07, // bLength: 7字节
	0x05, // bDescriptorType: ENDPOINT (0x05)
	0x06, // bEndpointAddress: OUT端点 0x06
	0x02, // bmAttributes: 批量端点
	0x40,
	0x00, // wMaxPacketSize: 64字节
	0x00, // bInterval: 忽略
};

// 语言描述符
const uint8_t MyLangDescr[] = { 0x04, 0x03, 0x09, 0x04 };
// 厂家信息
const uint8_t MyManuInfo[] = {
	0x0E, 0x03, // bLength = 14, bDescriptorType = 0x03
	'W',  0, // "W"
	'C',  0, // "C"
	'H',  0, // "H"
	'.',  0, // "."
	'C',  0, // "C"
	'N',  0 // "N"
};

// 产品信息
const uint8_t MyProdInfo[] = {
	0x22, 0x03, 'C', 0, 'H', 0, '5', 0, '8', 0, '2', 0,
	' ',  0,    'U', 0, 'S', 0, 'B', 0, ' ', 0, 'B', 0,
	'R',  0,    'I', 0, 'D', 0, 'G', 0, 'E', 0,
};

/**********************************************************/
uint8_t DevConfig, Ready;
uint8_t SetupReqCode;
uint16_t SetupReqLen;
const uint8_t *pDescr;
uint8_t Report_Value[USB_INTERFACE_MAX_INDEX + 1] = { 0x00 };
uint8_t Idle_Value[USB_INTERFACE_MAX_INDEX + 1] = { 0x00 };
uint8_t USB_SleepStatus = 0x00; /* USB睡眠状态 */

/******** 用户自定义分配端点RAM ****************************************/
__attribute__((aligned(4))) uint8_t EP0_Databuf[64];

__attribute__((aligned(4))) uint8_t EP1_Databuf[64];
__attribute__((aligned(4))) uint8_t EP2_Databuf[64];
__attribute__((aligned(4))) uint8_t EP3_Databuf[64];

__attribute__((aligned(4))) uint8_t EP5_Databuf[64];
__attribute__((aligned(4))) uint8_t EP6_Databuf[64];
__attribute__((aligned(4))) uint8_t EP7_Databuf[64];

#define DEF_USB_SET_LINE_CODING 0x20
#define DEF_USB_GET_LINE_CODING 0x21
#define DEF_USB_SET_LINE_STATE 0x22
#define DEF_USB_SEND_BREAK 0x23

/*********************************************************************
 * @fn      USB_DevTransProcess
 *
 * @brief   USB 传输处理函数
 *
 * @return  none
 */
void USB_DevTransProcess(void)
{
	uint8_t len, chtype;
	uint8_t intflag, errflag = 0;

	intflag = R8_USB_INT_FG;
	if (intflag & RB_UIF_TRANSFER) {
		if ((R8_USB_INT_ST & MASK_UIS_TOKEN) !=
		    MASK_UIS_TOKEN) // 非空闲
		{
			switch (R8_USB_INT_ST &
				(MASK_UIS_TOKEN | MASK_UIS_ENDP))
			// 分析操作令牌和端点号
			{
			case UIS_TOKEN_IN: {
				switch (SetupReqCode) {
				case USB_GET_DESCRIPTOR:
					len = SetupReqLen >= DevEP0SIZE ?
						      DevEP0SIZE :
						      SetupReqLen; // 本次传输长度
					memcpy(pEP0_DataBuf, pDescr,
					       len); /* 加载上传数据 */
					SetupReqLen -= len;
					pDescr += len;
					R8_UEP0_T_LEN = len;
					R8_UEP0_CTRL ^= RB_UEP_T_TOG; // 翻转
					break;
				case USB_SET_ADDRESS:
					R8_USB_DEV_AD = (R8_USB_DEV_AD &
							 RB_UDA_GP_BIT) |
							SetupReqLen;
					R8_UEP0_CTRL = UEP_R_RES_ACK |
						       UEP_T_RES_NAK;
					break;

				case USB_SET_FEATURE:
					break;

				default:
					R8_UEP0_T_LEN =
						0; // 状态阶段完成中断或者是强制上传0长度数据包结束控制传输
					R8_UEP0_CTRL = UEP_R_RES_ACK |
						       UEP_T_RES_NAK;
					break;
				}
			} break;

			case UIS_TOKEN_OUT: {
				len = R8_USB_RX_LEN;
				switch (usbdev_set_line_coding_pending) {
				case 0:
					memcpy(&usbdev_acm_0_line_coding,
					       pEP0_DataBuf, 7);
					break;
				case 2:
					memcpy(&usbdev_acm_1_line_coding,
					       pEP0_DataBuf, 7);
					if (usbdev_acm_1_mode ==
					    USBDEV_ACM1_MODE_UART0) {
						UART0_BaudRateCfg(
							usbdev_acm_1_line_coding
								.dwDTERate);
						switch (usbdev_acm_1_line_coding
								.bCharFormat) {
						case 0:
						case 1:
							R8_UART0_LCR &= ~(
								RB_LCR_STOP_BIT);
							break;
						case 2:
							R8_UART0_LCR |=
								(RB_LCR_STOP_BIT);
							break;
						}
						switch (usbdev_acm_1_line_coding
								.bParityType) {
						case 0:
							R8_UART0_LCR &= ~(
								((0b11) << 4) |
								(0b1 << 3));
							break;
						case 1:
							R8_UART0_LCR |=
								(1 << 3);
							R8_UART0_LCR &= ~(
								((0b11) << 4));
							break;
						case 2:
							R8_UART0_LCR |=
								(1 << 3);
							R8_UART0_LCR &= ~(
								((0b11) << 4));
							SAFEOPERATE;
							R8_UART0_LCR |=
								(0b01 << 4);
							break;
						case 3:
							R8_UART0_LCR |=
								(1 << 3);
							R8_UART0_LCR &= ~(
								((0b11) << 4));
							SAFEOPERATE;
							R8_UART0_LCR |=
								(0b10 << 4);
							break;
						case 4:
							R8_UART0_LCR |=
								(1 << 3);
							R8_UART0_LCR &= ~(
								((0b11) << 4));
							SAFEOPERATE;
							R8_UART0_LCR |=
								(0b11 << 4);
							break;
						}
						switch (usbdev_acm_1_line_coding
								.bDataBits) {
						case 5:
							R8_UART0_LCR &= ~(0b11);
							break;
						case 6:
							R8_UART0_LCR &= ~(0b11);
							SAFEOPERATE;
							R8_UART0_LCR |= (0b01);
							break;
						case 7:
							R8_UART0_LCR &= ~(0b11);
							SAFEOPERATE;
							R8_UART0_LCR |= (0b10);
							break;
						case 8:
							R8_UART0_LCR |= (0b11);
							break;
						}
					}
					break;
				}
			} break;

			case UIS_TOKEN_OUT | 1: {
				debug_puts("USBDEV FATAL: EP1 OUT\r\n");
				while (1)
					;
			} break;

			case UIS_TOKEN_IN | 1:
				R8_UEP1_CTRL ^= RB_UEP_T_TOG;
				R8_UEP1_CTRL =
					(R8_UEP1_CTRL & ~MASK_UEP_T_RES) |
					UEP_T_RES_NAK;
				break;

			case UIS_TOKEN_OUT | 2: {
				if (R8_USB_INT_ST &
				    RB_UIS_TOG_OK) { // 不同步的数据包将丢弃
					R8_UEP2_CTRL ^= RB_UEP_R_TOG;
					len = R8_USB_RX_LEN;
					DevEP2_OUT_Deal(len);
				}
			} break;

			case UIS_TOKEN_IN | 2:
				R8_UEP2_CTRL ^= RB_UEP_T_TOG;
				R8_UEP2_CTRL =
					(R8_UEP2_CTRL & ~MASK_UEP_T_RES) |
					UEP_T_RES_NAK;
				break;

			case UIS_TOKEN_OUT | 3: {
				debug_puts("USBDEV FATAL: EP3 OUT\r\n");
				while (1)
					;
			} break;

			case UIS_TOKEN_IN | 3:
				R8_UEP3_CTRL ^= RB_UEP_T_TOG;
				R8_UEP3_CTRL =
					(R8_UEP3_CTRL & ~MASK_UEP_T_RES) |
					UEP_T_RES_NAK;
				break;

			case UIS_TOKEN_OUT | 4: {
				debug_puts("USBDEV FATAL: EP4 OUT\r\n");
				while (1)
					;
			} break;

			case UIS_TOKEN_IN | 4:
				R8_UEP4_CTRL ^= RB_UEP_T_TOG;
				R8_UEP4_CTRL =
					(R8_UEP4_CTRL & ~MASK_UEP_T_RES) |
					UEP_T_RES_NAK;
				break;

			case UIS_TOKEN_OUT | 5: {
				debug_puts("USBDEV FATAL: EP5 OUT\r\n");
				while (1)
					;
			} break;

			case UIS_TOKEN_IN | 5:
				R8_UEP5_CTRL ^= RB_UEP_T_TOG;
				R8_UEP5_CTRL =
					(R8_UEP5_CTRL & ~MASK_UEP_T_RES) |
					UEP_T_RES_NAK;
				break;

			case UIS_TOKEN_OUT | 6: {
				if (R8_USB_INT_ST & RB_UIS_TOG_OK) {
					R8_UEP6_CTRL ^= RB_UEP_R_TOG;
					len = R8_USB_RX_LEN;
					DevEP6_OUT_Deal(len);
				}
			} break;

			case UIS_TOKEN_IN | 6:
				R8_UEP6_CTRL ^= RB_UEP_T_TOG;
				R8_UEP6_CTRL =
					(R8_UEP6_CTRL & ~MASK_UEP_T_RES) |
					UEP_T_RES_NAK;
				break;

			case UIS_TOKEN_OUT | 7: {
				debug_puts("USBDEV FATAL: EP7 OUT\r\n");
				while (1)
					;
			} break;

			case UIS_TOKEN_IN | 7:
				R8_UEP7_CTRL ^= RB_UEP_T_TOG;
				R8_UEP7_CTRL =
					(R8_UEP7_CTRL & ~MASK_UEP_T_RES) |
					UEP_T_RES_NAK;
				break;

			default:
				break;
			}
			R8_USB_INT_FG = RB_UIF_TRANSFER;
		}
		if (R8_USB_INT_ST & RB_UIS_SETUP_ACT) // Setup包处理
		{
			R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG |
				       UEP_R_RES_ACK | UEP_T_RES_NAK;
			SetupReqLen = pSetupReqPak->wLength;
			SetupReqCode = pSetupReqPak->bRequest;
			chtype = pSetupReqPak->bRequestType;

			len = 0;
			errflag = 0;
			if ((pSetupReqPak->bRequestType & USB_REQ_TYP_MASK) !=
			    USB_REQ_TYP_STANDARD) {
				/* 非标准请求 */
				/* 其它请求,如类请求，产商请求等 */
				if (pSetupReqPak->bRequestType & 0x40) {
					/* 厂商请求 */
				} else if (pSetupReqPak->bRequestType &
					   USB_REQ_TYP_CLASS) {
					/* CLASS REQ */
					switch (SetupReqCode) {
					case DEF_USB_SET_IDLE: /* 0x0A: SET_IDLE */ //主机想设置HID设备特定输入报表的空闲时间间隔
						Idle_Value[pSetupReqPak->wIndex] =
							(uint8_t)(pSetupReqPak
									  ->wValue >>
								  8);
						break; //这个一定要有

					case DEF_USB_SET_REPORT: /* 0x09: SET_REPORT */ //主机想设置HID设备的报表描述符
						break;

					case DEF_USB_SET_PROTOCOL: /* 0x0B: SET_PROTOCOL */ //主机想设置HID设备当前所使用的协议
						Report_Value[pSetupReqPak
								     ->wIndex] =
							(uint8_t)(pSetupReqPak
									  ->wValue);
						break;

					case DEF_USB_GET_IDLE: /* 0x02: GET_IDLE */ //主机想读取HID设备特定输入报表的当前的空闲比率
						EP0_Databuf[0] = Idle_Value
							[pSetupReqPak->wIndex];
						len = 1;
						break;

					case DEF_USB_GET_PROTOCOL: /* 0x03: GET_PROTOCOL */ //主机想获得HID设备当前所使用的协议
						EP0_Databuf[0] = Report_Value
							[pSetupReqPak->wIndex];
						len = 1;
						break;

					case DEF_USB_SET_LINE_CODING:
						usbdev_set_line_coding_pending =
							(pSetupReqPak->wIndex &
							 0xFF);
						len = 0;
						break;
					case DEF_USB_GET_LINE_CODING:
						len = 0;
						switch (pSetupReqPak->wIndex &
							0xFF) {
						case 0:
							len = sizeof(
								usbdev_acm_0_line_coding);
							memcpy(pEP0_DataBuf,
							       &usbdev_acm_0_line_coding,
							       len);
							break;
						case 2:
							len = sizeof(
								usbdev_acm_1_line_coding);
							memcpy(pEP0_DataBuf,
							       &usbdev_acm_1_line_coding,
							       len);
							break;
						}
						break;
					case DEF_USB_SET_LINE_STATE:
						len = 0;
						break;
					case DEF_USB_SEND_BREAK:
						len = 0;
						break;
					default:
						errflag = 0xFF;
					}
				}
			} else /* 标准请求 */
			{
				switch (SetupReqCode) {
				case USB_GET_DESCRIPTOR: {
					switch (((pSetupReqPak->wValue) >> 8)) {
					case USB_DESCR_TYP_DEVICE: {
						pDescr = MyDevDescr;
						len = MyDevDescr[0];
					} break;

					case USB_DESCR_TYP_CONFIG: {
						pDescr = MyCfgDescr;
						len = MyCfgDescr[2];
					} break;

					case USB_DESCR_TYP_STRING: {
						switch ((pSetupReqPak->wValue) &
							0xff) {
						case 1:
							pDescr = MyManuInfo;
							len = MyManuInfo[0];
							break;
						case 2:
							pDescr = MyProdInfo;
							len = MyProdInfo[0];
							break;
						case 0:
							pDescr = MyLangDescr;
							len = MyLangDescr[0];
							break;
						default:
							errflag =
								0xFF; // 不支持的字符串描述符
							break;
						}
					} break;

					default:
						errflag = 0xff;
						break;
					}
					if (SetupReqLen > len)
						SetupReqLen =
							len; //实际需上传总长度
					len = (SetupReqLen >= DevEP0SIZE) ?
						      DevEP0SIZE :
						      SetupReqLen;
					memcpy(pEP0_DataBuf, pDescr, len);
					pDescr += len;
				} break;

				case USB_SET_ADDRESS:
					SetupReqLen = (pSetupReqPak->wValue) &
						      0xff;
					break;

				case USB_GET_CONFIGURATION:
					pEP0_DataBuf[0] = DevConfig;
					if (SetupReqLen > 1)
						SetupReqLen = 1;
					break;

				case USB_SET_CONFIGURATION:
					DevConfig = (pSetupReqPak->wValue) &
						    0xff;
					break;

				case USB_CLEAR_FEATURE: {
					if ((pSetupReqPak->bRequestType &
					     USB_REQ_RECIP_MASK) ==
					    USB_REQ_RECIP_ENDP) // 端点
					{
						switch ((pSetupReqPak->wIndex) &
							0xff) {
						case 0x87:
							R8_UEP7_CTRL =
								(R8_UEP7_CTRL &
								 ~(RB_UEP_T_TOG |
								   MASK_UEP_T_RES)) |
								UEP_T_RES_NAK;
							break;
						case 0x07:
							R8_UEP7_CTRL =
								(R8_UEP7_CTRL &
								 ~(RB_UEP_R_TOG |
								   MASK_UEP_R_RES)) |
								UEP_R_RES_ACK;
							break;
						case 0x86:
							R8_UEP6_CTRL =
								(R8_UEP6_CTRL &
								 ~(RB_UEP_T_TOG |
								   MASK_UEP_T_RES)) |
								UEP_T_RES_NAK;
							break;
						case 0x06:
							R8_UEP6_CTRL =
								(R8_UEP6_CTRL &
								 ~(RB_UEP_R_TOG |
								   MASK_UEP_R_RES)) |
								UEP_R_RES_ACK;
							break;
						case 0x85:
							R8_UEP5_CTRL =
								(R8_UEP5_CTRL &
								 ~(RB_UEP_T_TOG |
								   MASK_UEP_T_RES)) |
								UEP_T_RES_NAK;
							break;
						case 0x05:
							R8_UEP5_CTRL =
								(R8_UEP5_CTRL &
								 ~(RB_UEP_R_TOG |
								   MASK_UEP_R_RES)) |
								UEP_R_RES_ACK;
							break;
						case 0x84:
							R8_UEP4_CTRL =
								(R8_UEP4_CTRL &
								 ~(RB_UEP_T_TOG |
								   MASK_UEP_T_RES)) |
								UEP_T_RES_NAK;
							break;
						case 0x04:
							R8_UEP4_CTRL =
								(R8_UEP4_CTRL &
								 ~(RB_UEP_R_TOG |
								   MASK_UEP_R_RES)) |
								UEP_R_RES_ACK;
							break;
						case 0x83:
							R8_UEP3_CTRL =
								(R8_UEP3_CTRL &
								 ~(RB_UEP_T_TOG |
								   MASK_UEP_T_RES)) |
								UEP_T_RES_NAK;
							break;
						case 0x03:
							R8_UEP3_CTRL =
								(R8_UEP3_CTRL &
								 ~(RB_UEP_R_TOG |
								   MASK_UEP_R_RES)) |
								UEP_R_RES_ACK;
							break;
						case 0x82:
							R8_UEP2_CTRL =
								(R8_UEP2_CTRL &
								 ~(RB_UEP_T_TOG |
								   MASK_UEP_T_RES)) |
								UEP_T_RES_NAK;
							break;
						case 0x02:
							R8_UEP2_CTRL =
								(R8_UEP2_CTRL &
								 ~(RB_UEP_R_TOG |
								   MASK_UEP_R_RES)) |
								UEP_R_RES_ACK;
							break;
						case 0x81:
							R8_UEP1_CTRL =
								(R8_UEP1_CTRL &
								 ~(RB_UEP_T_TOG |
								   MASK_UEP_T_RES)) |
								UEP_T_RES_NAK;
							break;
						case 0x01:
							R8_UEP1_CTRL =
								(R8_UEP1_CTRL &
								 ~(RB_UEP_R_TOG |
								   MASK_UEP_R_RES)) |
								UEP_R_RES_ACK;
							break;
						default:
							errflag =
								0xFF; // 不支持的端点
							break;
						}
					} else if ((pSetupReqPak->bRequestType &
						    USB_REQ_RECIP_MASK) ==
						   USB_REQ_RECIP_DEVICE) {
						if (pSetupReqPak->wValue == 1) {
							USB_SleepStatus &=
								~0x01;
						}
					} else {
						errflag = 0xFF;
					}
				} break;

				case USB_SET_FEATURE:
					if ((pSetupReqPak->bRequestType &
					     USB_REQ_RECIP_MASK) ==
					    USB_REQ_RECIP_ENDP) {
						/* 端点 */
						switch (pSetupReqPak->wIndex) {
						case 0x87:
							R8_UEP7_CTRL =
								(R8_UEP7_CTRL &
								 ~(RB_UEP_T_TOG |
								   MASK_UEP_T_RES)) |
								UEP_T_RES_STALL;
							break;
						case 0x07:
							R8_UEP7_CTRL =
								(R8_UEP7_CTRL &
								 ~(RB_UEP_R_TOG |
								   MASK_UEP_R_RES)) |
								UEP_R_RES_STALL;
							break;
						case 0x86:
							R8_UEP6_CTRL =
								(R8_UEP6_CTRL &
								 ~(RB_UEP_T_TOG |
								   MASK_UEP_T_RES)) |
								UEP_T_RES_STALL;
							break;
						case 0x06:
							R8_UEP6_CTRL =
								(R8_UEP6_CTRL &
								 ~(RB_UEP_R_TOG |
								   MASK_UEP_R_RES)) |
								UEP_R_RES_STALL;
							break;
						case 0x85:
							R8_UEP5_CTRL =
								(R8_UEP5_CTRL &
								 ~(RB_UEP_T_TOG |
								   MASK_UEP_T_RES)) |
								UEP_T_RES_STALL;
							break;
						case 0x05:
							R8_UEP5_CTRL =
								(R8_UEP5_CTRL &
								 ~(RB_UEP_R_TOG |
								   MASK_UEP_R_RES)) |
								UEP_R_RES_STALL;
							break;
						case 0x84:
							R8_UEP4_CTRL =
								(R8_UEP4_CTRL &
								 ~(RB_UEP_T_TOG |
								   MASK_UEP_T_RES)) |
								UEP_T_RES_STALL;
							break;
						case 0x04:
							R8_UEP4_CTRL =
								(R8_UEP4_CTRL &
								 ~(RB_UEP_R_TOG |
								   MASK_UEP_R_RES)) |
								UEP_R_RES_STALL;
							break;
						case 0x83:
							R8_UEP3_CTRL =
								(R8_UEP3_CTRL &
								 ~(RB_UEP_T_TOG |
								   MASK_UEP_T_RES)) |
								UEP_T_RES_STALL;
							break;
						case 0x03:
							R8_UEP3_CTRL =
								(R8_UEP3_CTRL &
								 ~(RB_UEP_R_TOG |
								   MASK_UEP_R_RES)) |
								UEP_R_RES_STALL;
							break;
						case 0x82:
							R8_UEP2_CTRL =
								(R8_UEP2_CTRL &
								 ~(RB_UEP_T_TOG |
								   MASK_UEP_T_RES)) |
								UEP_T_RES_STALL;
							break;
						case 0x02:
							R8_UEP2_CTRL =
								(R8_UEP2_CTRL &
								 ~(RB_UEP_R_TOG |
								   MASK_UEP_R_RES)) |
								UEP_R_RES_STALL;
							break;
						case 0x81:
							R8_UEP1_CTRL =
								(R8_UEP1_CTRL &
								 ~(RB_UEP_T_TOG |
								   MASK_UEP_T_RES)) |
								UEP_T_RES_STALL;
							break;
						case 0x01:
							R8_UEP1_CTRL =
								(R8_UEP1_CTRL &
								 ~(RB_UEP_R_TOG |
								   MASK_UEP_R_RES)) |
								UEP_R_RES_STALL;
							break;
						default:
							/* 不支持的端点 */
							errflag =
								0xFF; // 不支持的端点
							break;
						}
					} else if ((pSetupReqPak->bRequestType &
						    USB_REQ_RECIP_MASK) ==
						   USB_REQ_RECIP_DEVICE) {
						if (pSetupReqPak->wValue == 1) {
							/* 设置睡眠 */
							USB_SleepStatus |= 0x01;
						}
					} else {
						errflag = 0xFF;
					}
					break;

				case USB_GET_INTERFACE:
					pEP0_DataBuf[0] = 0x00;
					if (SetupReqLen > 1)
						SetupReqLen = 1;
					break;

				case USB_SET_INTERFACE:
					break;

				case USB_GET_STATUS:
					if ((pSetupReqPak->bRequestType &
					     USB_REQ_RECIP_MASK) ==
					    USB_REQ_RECIP_ENDP) {
						/* 端点 */
						pEP0_DataBuf[0] = 0x00;
						switch (pSetupReqPak->wIndex) {
						case 0x87:
							if ((R8_UEP7_CTRL &
							     (RB_UEP_T_TOG |
							      MASK_UEP_T_RES)) ==
							    UEP_T_RES_STALL) {
								pEP0_DataBuf[0] =
									0x01;
							}
							break;

						case 0x07:
							if ((R8_UEP7_CTRL &
							     (RB_UEP_R_TOG |
							      MASK_UEP_R_RES)) ==
							    UEP_R_RES_STALL) {
								pEP0_DataBuf[0] =
									0x01;
							}
							break;
						case 0x86:
							if ((R8_UEP6_CTRL &
							     (RB_UEP_T_TOG |
							      MASK_UEP_T_RES)) ==
							    UEP_T_RES_STALL) {
								pEP0_DataBuf[0] =
									0x01;
							}
							break;

						case 0x06:
							if ((R8_UEP6_CTRL &
							     (RB_UEP_R_TOG |
							      MASK_UEP_R_RES)) ==
							    UEP_R_RES_STALL) {
								pEP0_DataBuf[0] =
									0x01;
							}
							break;
						case 0x85:
							if ((R8_UEP5_CTRL &
							     (RB_UEP_T_TOG |
							      MASK_UEP_T_RES)) ==
							    UEP_T_RES_STALL) {
								pEP0_DataBuf[0] =
									0x01;
							}
							break;

						case 0x05:
							if ((R8_UEP5_CTRL &
							     (RB_UEP_R_TOG |
							      MASK_UEP_R_RES)) ==
							    UEP_R_RES_STALL) {
								pEP0_DataBuf[0] =
									0x01;
							}
							break;

						case 0x84:
							if ((R8_UEP4_CTRL &
							     (RB_UEP_T_TOG |
							      MASK_UEP_T_RES)) ==
							    UEP_T_RES_STALL) {
								pEP0_DataBuf[0] =
									0x01;
							}
							break;

						case 0x04:
							if ((R8_UEP4_CTRL &
							     (RB_UEP_R_TOG |
							      MASK_UEP_R_RES)) ==
							    UEP_R_RES_STALL) {
								pEP0_DataBuf[0] =
									0x01;
							}
							break;

						case 0x83:
							if ((R8_UEP3_CTRL &
							     (RB_UEP_T_TOG |
							      MASK_UEP_T_RES)) ==
							    UEP_T_RES_STALL) {
								pEP0_DataBuf[0] =
									0x01;
							}
							break;

						case 0x03:
							if ((R8_UEP3_CTRL &
							     (RB_UEP_R_TOG |
							      MASK_UEP_R_RES)) ==
							    UEP_R_RES_STALL) {
								pEP0_DataBuf[0] =
									0x01;
							}
							break;

						case 0x82:
							if ((R8_UEP2_CTRL &
							     (RB_UEP_T_TOG |
							      MASK_UEP_T_RES)) ==
							    UEP_T_RES_STALL) {
								pEP0_DataBuf[0] =
									0x01;
							}
							break;

						case 0x02:
							if ((R8_UEP2_CTRL &
							     (RB_UEP_R_TOG |
							      MASK_UEP_R_RES)) ==
							    UEP_R_RES_STALL) {
								pEP0_DataBuf[0] =
									0x01;
							}
							break;

						case 0x81:
							if ((R8_UEP1_CTRL &
							     (RB_UEP_T_TOG |
							      MASK_UEP_T_RES)) ==
							    UEP_T_RES_STALL) {
								pEP0_DataBuf[0] =
									0x01;
							}
							break;

						case 0x01:
							if ((R8_UEP1_CTRL &
							     (RB_UEP_R_TOG |
							      MASK_UEP_R_RES)) ==
							    UEP_R_RES_STALL) {
								pEP0_DataBuf[0] =
									0x01;
							}
							break;
						}
					} else if ((pSetupReqPak->bRequestType &
						    USB_REQ_RECIP_MASK) ==
						   USB_REQ_RECIP_DEVICE) {
						pEP0_DataBuf[0] = 0x00;
						if (USB_SleepStatus) {
							pEP0_DataBuf[0] = 0x02;
						} else {
							pEP0_DataBuf[0] = 0x00;
						}
					}
					pEP0_DataBuf[1] = 0;
					if (SetupReqLen >= 2) {
						SetupReqLen = 2;
					}
					break;

				default:
					errflag = 0xff;
					break;
				}
			}
			if (errflag == 0xff) // 错误或不支持
			{
				//                  SetupReqCode = 0xFF;
				R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG |
					       UEP_R_RES_STALL |
					       UEP_T_RES_STALL; // STALL
			} else {
				if (chtype & 0x80) // 上传
				{
					len = (SetupReqLen > DevEP0SIZE) ?
						      DevEP0SIZE :
						      SetupReqLen;
					SetupReqLen -= len;
				} else
					len = 0; // 下传
				R8_UEP0_T_LEN = len;
				R8_UEP0_CTRL =
					RB_UEP_R_TOG | RB_UEP_T_TOG |
					UEP_R_RES_ACK |
					UEP_T_RES_ACK; // 默认数据包是DATA1
			}

			R8_USB_INT_FG = RB_UIF_TRANSFER;
		}
	} else if (intflag & RB_UIF_BUS_RST) {
		R8_USB_DEV_AD = 0;
		R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
		R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
		R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
		R8_UEP3_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
		R8_UEP4_CTRL = UEP_R_RES_STALL | UEP_T_RES_STALL;
		R8_UEP5_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
		R8_UEP6_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
		R8_UEP7_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
		R8_USB_INT_FG = RB_UIF_BUS_RST;
	} else if (intflag & RB_UIF_SUSPEND) {
		if (R8_USB_MIS_ST & RB_UMS_SUSPEND) {
			;
		} // 挂起
		else {
			;
		} // 唤醒
		R8_USB_INT_FG = RB_UIF_SUSPEND;
	} else {
		R8_USB_INT_FG = intflag;
	}
}

/*********************************************************************
 * @fn      DevWakeup
 *
 * @brief   设备模式唤醒主机
 *
 * @return  none
 */
void DevWakeup(void)
{
	R16_PIN_ANALOG_IE &= ~(RB_PIN_USB_DP_PU);
	R8_UDEV_CTRL |= RB_UD_LOW_SPEED;
	mDelaymS(2);
	R8_UDEV_CTRL &= ~RB_UD_LOW_SPEED;
	R16_PIN_ANALOG_IE |= RB_PIN_USB_DP_PU;
}

void DevEP2_OUT_Deal(uint8_t len)
{
	uint8_t idx = 0;
	while (idx < len) {
		fifo8_push(&usbdev_acm_0_h2d_fifo, EP2_Databuf[idx]);
		idx += 1;
	}

	if (fifo8_num_free(&usbdev_acm_0_h2d_fifo) < (8 * 2)) {
		cdc_acm_0_h2d_pause = true;
		//R8_UEP2_CTRL &= ~UEP_R_RES_STALL;
		R8_UEP2_CTRL |= UEP_R_RES_NAK;
	}
	cdc_acm_0_h2d_total += len;
}

void DevEP6_OUT_Deal(uint8_t len)
{
	uint8_t idx = 0;
	while (idx < len) {
		fifo8_push(&usbdev_acm_1_h2d_fifo, EP6_Databuf[idx]);
		idx += 1;
	}

	if (fifo8_num_free(&usbdev_acm_1_h2d_fifo) < (64 * 2)) {
		cdc_acm_1_h2d_pause = true;
		//R8_UEP6_CTRL &= ~UEP_R_RES_STALL;
		R8_UEP6_CTRL |= UEP_R_RES_NAK;
	}
	cdc_acm_1_h2d_total += len;
}

/*********************************************************************
 * @fn      USB_IRQHandler
 *
 * @brief   USB中断函数
 *
 * @return  none
 */
__INTERRUPT
__HIGH_CODE
void USB_IRQHandler(void) /* USB中断服务程序,使用寄存器组1 */
{
	USB_DevTransProcess();
}

void usbdev_init(void)
{
	usbdev_acm_0_line_coding.dwDTERate = (12 * 1000 * 1000);
	usbdev_acm_0_line_coding.bDataBits = 8;
	usbdev_acm_0_line_coding.bParityType = 0;
	usbdev_acm_0_line_coding.bCharFormat = 0;

	usbdev_acm_1_line_coding.dwDTERate = 1200;
	usbdev_acm_1_line_coding.bDataBits = 8;
	usbdev_acm_1_line_coding.bParityType = 0;
	usbdev_acm_1_line_coding.bCharFormat = 0;

	pEP0_RAM_Addr = EP0_Databuf;
	R8_USB_CTRL = 0x00; // 先设定模式,取消 RB_UC_CLR_ALL

	R8_UEP4_1_MOD = RB_UEP1_TX_EN;
	R8_UEP2_3_MOD = RB_UEP2_RX_EN | RB_UEP3_TX_EN;
	R8_UEP567_MOD = RB_UEP5_TX_EN | RB_UEP6_RX_EN | RB_UEP7_TX_EN;

	R16_UEP0_DMA = (uint16_t)(uint32_t)pEP0_RAM_Addr;

	R16_UEP1_DMA = (uint16_t)(uint32_t)EP1_Databuf;
	R16_UEP2_DMA = (uint16_t)(uint32_t)EP2_Databuf;
	R16_UEP3_DMA = (uint16_t)(uint32_t)EP3_Databuf;

	R16_UEP5_DMA = (uint16_t)(uint32_t)EP5_Databuf;
	R16_UEP6_DMA = (uint16_t)(uint32_t)EP6_Databuf;
	R16_UEP7_DMA = (uint16_t)(uint32_t)EP7_Databuf;

	R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
	R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
	R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
	R8_UEP3_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
	R8_UEP4_CTRL = UEP_R_RES_STALL | UEP_T_RES_STALL;
	R8_UEP5_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
	R8_UEP6_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
	R8_UEP7_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;

	R8_USB_DEV_AD = 0x00;
	R8_USB_CTRL =
		RB_UC_DEV_PU_EN | RB_UC_INT_BUSY |
		RB_UC_DMA_EN; // 启动USB设备及DMA，在中断期间中断标志未清除前自动返回NAK
	R16_PIN_ANALOG_IE |= RB_PIN_USB_IE |
			     RB_PIN_USB_DP_PU; // 防止USB端口浮空及上拉电阻
	R8_USB_INT_FG = 0xFF; // 清中断标志
	R8_UDEV_CTRL = RB_UD_PD_DIS | RB_UD_PORT_EN; // 允许USB端口
	R8_USB_INT_EN = RB_UIE_SUSPEND | RB_UIE_BUS_RST | RB_UIE_TRANSFER;

	PFIC_EnableIRQ(USB_IRQn);
}

void usbdev_task(void)
{
	int idx;
	uint8_t c;
#if 0
	// ECHO TEST
	while (fifo8_pop(&usbdev_acm_0_h2d_fifo, &c) &&
	       (fifo8_num_free(&usbdev_acm_0_d2h_fifo) > 0)) {
		fifo8_push(&usbdev_acm_0_d2h_fifo, c);
	}
	while (fifo8_pop(&usbdev_acm_1_h2d_fifo, &c) &&
	       (fifo8_num_free(&usbdev_acm_1_d2h_fifo) > 0)) {
		fifo8_push(&usbdev_acm_1_d2h_fifo, c);
	}
#endif
	if (EP1_GetINSta()) {
		static uint8_t ep1_prev_in_len = 0; // ZLP
		idx = 0;
		while (idx < 8) {
			if (fifo8_pop(&usbdev_acm_0_d2h_fifo,
				      &EP1_Databuf[idx]) == false) {
				break;
			}
			idx += 1;
		}
		if ((idx > 0) || (ep1_prev_in_len == 8)) {
			DevEP1_IN_Deal(idx);
			ep1_prev_in_len = idx;
			cdc_acm_0_d2h_total += idx;
		}
	}

	if (EP5_GetINSta()) {
		static uint8_t ep5_prev_in_len = 0; // ZLP
		idx = 0;
		while (idx < 64) {
			if (fifo8_pop(&usbdev_acm_1_d2h_fifo,
				      &EP5_Databuf[idx]) == false) {
				break;
			}
			idx += 1;
		}
		if ((idx > 0) || (ep5_prev_in_len == 64)) {
			DevEP5_IN_Deal(idx);
			ep5_prev_in_len = idx;
			cdc_acm_1_d2h_total += idx;
		}
	}
	if ((fifo8_num_free(&usbdev_acm_0_h2d_fifo) > (8 * 2)) &&
	    (cdc_acm_0_h2d_pause == true)) {
		cdc_acm_0_h2d_pause = false;
		PFIC_DisableIRQ(USB_IRQn);
		R8_UEP2_CTRL &= ~UEP_R_RES_STALL;
		R8_UEP2_CTRL |= UEP_R_RES_ACK;
		PFIC_EnableIRQ(USB_IRQn);
	}
	if ((fifo8_num_free(&usbdev_acm_1_h2d_fifo) > (64 * 2)) &&
	    (cdc_acm_1_h2d_pause == true)) {
		cdc_acm_1_h2d_pause = false;
		PFIC_DisableIRQ(USB_IRQn);
		R8_UEP6_CTRL &= ~UEP_R_RES_STALL;
		R8_UEP6_CTRL |= UEP_R_RES_ACK;
		PFIC_EnableIRQ(USB_IRQn);
	}
	int forth_taskidx;

	if (fifo8_num_used(&usbdev_acm_0_h2d_fifo) > 0) {
		forth_taskidx = 0;
		while (forth_taskidx < FORTH_TASK_MAX) {
			if (forth_tasks[forth_taskidx] != NULL) {
				if (forth_tasks[forth_taskidx]->wait_state ==
				    FORTH_WAIT_ACM0_KEY) {
					tmos_set_event(
						forth_tasks[forth_taskidx]
							->taskid,
						FORTH_EVT_RUN);
				}
			}
			forth_taskidx += 1;
		}
	}
	if (fifo8_num_free(&usbdev_acm_0_d2h_fifo) > 0) {
		forth_taskidx = 0;
		while (forth_taskidx < FORTH_TASK_MAX) {
			if (forth_tasks[forth_taskidx] != NULL) {
				if (forth_tasks[forth_taskidx]->wait_state ==
				    FORTH_WAIT_ACM0_EMIT) {
					tmos_set_event(
						forth_tasks[forth_taskidx]
							->taskid,
						FORTH_EVT_RUN);
				}
			}
			forth_taskidx += 1;
		}
	}
	if (fifo8_num_used(&usbdev_acm_1_h2d_fifo) > 0) {
		forth_taskidx = 0;
		while (forth_taskidx < FORTH_TASK_MAX) {
			if (forth_tasks[forth_taskidx] != NULL) {
				if (forth_tasks[forth_taskidx]->wait_state ==
				    FORTH_WAIT_ACM1_KEY) {
					tmos_set_event(
						forth_tasks[forth_taskidx]
							->taskid,
						FORTH_EVT_RUN);
				}
			}
			forth_taskidx += 1;
		}
	}
	if (fifo8_num_free(&usbdev_acm_1_d2h_fifo) > 0) {
		forth_taskidx = 0;
		while (forth_taskidx < FORTH_TASK_MAX) {
			if (forth_tasks[forth_taskidx] != NULL) {
				if (forth_tasks[forth_taskidx]->wait_state ==
				    FORTH_WAIT_ACM1_EMIT) {
					tmos_set_event(
						forth_tasks[forth_taskidx]
							->taskid,
						FORTH_EVT_RUN);
				}
			}
			forth_taskidx += 1;
		}
	}
}
