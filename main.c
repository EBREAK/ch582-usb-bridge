#include "config.h"
#include "HAL.h"
#include "usbdev.h"
#include "debug.h"

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

__HIGH_CODE
__attribute__((noinline))
void Main_Circulation()
{
	while (1) {
		TMOS_SystemProcess();
	}
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
	Main_Circulation();
}
