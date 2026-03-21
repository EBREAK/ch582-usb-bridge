CROSS_COMPILE ?= riscv32-qingkev4a-elf-
CC = $(CROSS_COMPILE)gcc
OD = $(CROSS_COMPILE)objdump
OC = $(CROSS_COMPILE)objcopy
SZ = $(CROSS_COMPILE)size

FW_NAME ?= cdc-acm

CH583_SDK ?= ./EVT/EXAM/

#LINK_SCRIPT ?= $(CH583_SDK)/Ld/Link.ld
LINK_SCRIPT ?= Link.ld

#STARTUP_SCRIPT ?= $(CH583_SDK)/SRC/Startup/startup_CH583.S
STARTUP_SCRIPT ?= startup_CH583.S

SRCS += \
	$(CH583_SDK)/SRC/RVMSIS/core_riscv.c \
	$(CH583_SDK)/SRC/StdPeriphDriver/CH58x_sys.c \
	$(CH583_SDK)/SRC/StdPeriphDriver/CH58x_pwr.c \
	$(CH583_SDK)/SRC/StdPeriphDriver/CH58x_clk.c \
	$(CH583_SDK)/SRC/StdPeriphDriver/CH58x_flash.c \
	$(CH583_SDK)/SRC/StdPeriphDriver/CH58x_gpio.c \
	$(CH583_SDK)/SRC/StdPeriphDriver/CH58x_timer0.c \
	$(CH583_SDK)/SRC/StdPeriphDriver/CH58x_uart0.c \
	$(CH583_SDK)/SRC/StdPeriphDriver/CH58x_uart1.c \
	$(CH583_SDK)/SRC/StdPeriphDriver/CH58x_uart2.c \
	$(CH583_SDK)/SRC/StdPeriphDriver/CH58x_uart3.c \
	$(CH583_SDK)/SRC/StdPeriphDriver/CH58x_adc.c \
	$(CH583_SDK)/SRC/StdPeriphDriver/CH58x_usbdev.c \
	$(CH583_SDK)/BLE/HAL/SLEEP.c \
	$(CH583_SDK)/BLE/HAL/MCU.c \
	$(CH583_SDK)/BLE/HAL/RTC.c \

INCS += \
	-I $(CH583_SDK)/SRC/RVMSIS \
	-I $(CH583_SDK)/SRC/StdPeriphDriver/inc \
	-I $(CH583_SDK)/BLE/HAL/include \
	-I $(CH583_SDK)/BLE/LIB \

LIBS += \
	-L $(CH583_SDK)/SRC/StdPeriphDriver \
	-lISP583 \

CFLAGS += \
	-pipe \
	-Wall -Wextra \
	-Wno-unused-variable \
	-Wno-unused-parameter \
	-Wno-pointer-to-int-cast \
	-Wno-discarded-qualifiers \
	-Xlinker --gc-sections \
	-march=rv32imac_zicsr_zifencei -mabi=ilp32 \
	-Os -ggdb \
	--specs=picolibc.specs \
	-nostartfiles \
	-T $(LINK_SCRIPT) \

SRCS += \
	debug.c \
	main.c \

CFLAGS += \
	-DINT_SOFT \
	-DCH58xBLE_ROM=1 \
	-DLIB_FLASH_BASE_ADDRESSS=0x00040000 \

all:
	$(CC) $(CFLAGS) $(STARTUP_SCRIPT) $(INCS) $(SRCS) $(LIBS) -o $(FW_NAME).elf
	$(OD) -l -F -S -d $(FW_NAME).elf > $(FW_NAME).dis
	$(OC) -O binary $(FW_NAME).elf $(FW_NAME).bin
	$(SZ) $(FW_NAME).elf
	./host/mkhdr.exe $(FW_NAME).bin
	cp $(FW_NAME).bin $(FW_NAME)_enc.bin
	./host/fwenc.exe $(FW_NAME)_enc.bin

patch:
	sed -i -e 's/CH58xBLE_LIB.H/CH58xBLE_LIB.h/g' \
		$(CH583_SDK)/BLE/HAL/include/config.h
	sed -i -e 's/CONFIG.h/config.h/g' \
		$(CH583_SDK)/BLE/HAL/include/HAL.h
	sed -i -e 's/CONFIG.h/config.h/g' \
		$(CH583_SDK)/BLE/HAL/include/SLEEP.h
	sed -i -e 's/CH58xBLE_ROM.H/CH58xBLE_ROM.h/g' \
		$(CH583_SDK)/BLE/HAL/include/config.h
	sed -i -e 's/ptrdiff_t/int32_t/g' \
		$(CH583_SDK)/SRC/StdPeriphDriver/CH58x_sys.c 

flash-ble-stack:
	wlink flash $(CH583_SDK)/BLE/LIB/CH58xBLE_ROMx.hex

flash:
	wlink flash -a 0x8000 $(FW_NAME).bin
