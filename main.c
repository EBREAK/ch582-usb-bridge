#include "config.h"
#include "HAL.h"
#include "usbdev.h"
#include "main.h"
#include "debug.h"

uint8_t main_taskid;

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

__HIGH_CODE
__attribute__((noinline)) void Main_Circulation()
{
	while (1) {
		TMOS_SystemProcess();
		usbdev_task();
	}
}

uint16_t Main_ProcessEvent(uint8_t task_id, uint16_t events)
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

	if (events & MAIN_EVT_START) {
		tmos_start_task(main_taskid, MAIN_EVT_PERIODIC1S, 1600);
		return (events ^ MAIN_EVT_START);
	}

	if (events & MAIN_EVT_PERIODIC1S) {
		usbdev_show_stat();
		tmos_start_task(main_taskid, MAIN_EVT_PERIODIC1S, 1600);
		return (events ^ MAIN_EVT_PERIODIC1S);
	}

	return 0;
}

int main(void)
{
#if (defined(DCDC_ENABLE)) && (DCDC_ENABLE == TRUE)
	PWR_DCDCCfg(ENABLE);
#endif
	SetSysClock(CLK_SOURCE_PLL_60MHz);
#if (defined(HAL_SLEEP)) && (HAL_SLEEP == TRUE)
	GPIOA_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
	GPIOB_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
#endif
	GPIOA_SetBits(bTXD1);
	GPIOA_ModeCfg(bTXD1, GPIO_ModeOut_PP_5mA);
	UART1_DefInit();
	CH58X_BLEInit();
	debug_puts((char *)VER_LIB);
	debug_cr();
	HAL_Init();
	usbdev_init();
	debug_puts("CH582 USB BRIDGE\r\n");
	main_taskid = TMOS_ProcessEventRegister(Main_ProcessEvent);
	tmos_set_event(main_taskid, MAIN_EVT_START);
	Main_Circulation();
}
