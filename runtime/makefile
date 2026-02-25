# Makefile — TiZi PLC Runtime A (宿主固件)
#
# 构建目标：firmware.bin → 烧录到 Flash A (0x00000000, 16KB)
# 用户逻辑：在 user_logic/ 子目录下独立构建，生成 user_logic.bin
#           由上位机通过 UART 协议下载到 Flash B (0x00004000, 16KB)

# 输出目录
OUTPUT_DIR := build

# 默认 Linux USB 串口（上传时覆盖：make upload TTY=/dev/ttyUSB0）
TTY ?= /dev/ttyACM*

# ARM Cortex-M0+ 交叉编译器
TRGT = arm-none-eabi-
CC   = $(TRGT)gcc
CXX  = $(TRGT)g++
CP   = $(TRGT)objcopy
DUMP = $(TRGT)objdump

# 编译选项
#   -I./            使 shared_interface.h 可通过 #include "shared_interface.h" 找到
#   -I./bsp/include 使 bsp 头文件可找到
#   -I./bsp/lpc_chip 供 lpc_chip 内部互相引用
CFLAGS   = -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
CFLAGS  += -I./ -I./bsp -I./bsp/include -I./bsp/lpc_chip
CFLAGS  += -Os -ggdb
CFLAGS  += -ffunction-sections -fdata-sections
CFLAGS  += -Wall -Wextra
CXXFLAGS = $(CFLAGS) -fno-rtti -fno-exceptions

# 链接选项
#   -Wl,--gc-sections 配合 -ffunction-sections 去除未引用的代码
LDFLAGS  = -Wl,--script=./bsp/include/lpc824.ld
LDFLAGS += -nostartfiles -mfloat-abi=soft -nolibc
LDFLAGS += -Wl,--gc-sections

# vpath 让 make 在这些目录里查找 .c / .h
vpath %.c bsp/lpc_chip app
vpath %.h bsp/lpc_chip bsp/include app

# -----------------------------------------------------------------------
# Runtime A 源文件列表
# -----------------------------------------------------------------------
SOURCE_FILES := \
    main.c            \
    libutil.c         \
    app/runtime.c     \
    gcc_startup_lpc82x.c  \
    board_sysinit.c   \
    board.c           \
    board_api_stubs_8xx.c \
    crp.c             \
    mtb.c             \
    sysinit.c         \
    acmp_8xx.c        \
    adc_8xx.c         \
    chip_8xx.c        \
    clock_8xx.c       \
    crc_8xx.c         \
    dma_8xx.c         \
    eeprom.c          \
    gpio_8xx.c        \
    i2c_common_8xx.c  \
    i2cm_8xx.c        \
    i2cs_8xx.c        \
    iap.c             \
    iocon_8xx.c       \
    irc_8xx.c         \
    pinint_8xx.c      \
    pmu_8xx.c         \
    ring_buffer.c     \
    rtc_ut.c          \
    sct_8xx.c         \
    sct_pwm_8xx.c     \
    spi_8xx.c         \
    spim_8xx.c        \
    spis_8xx.c        \
    stopwatch_8xx.c   \
    swm_8xx.c         \
    syscon_8xx.c      \
    sysinit_8xx.c     \
    uart_8xx.c        \
    wkt_8xx.c         \
    wwdt_8xx.c

# 目标文件列表（保留子目录路径）
OBJECT_FILES := $(addprefix $(OUTPUT_DIR)/, $(patsubst %.c, %.o, $(SOURCE_FILES)))

# -----------------------------------------------------------------------
# 默认目标：构建 Runtime A
# -----------------------------------------------------------------------
.PHONY: all user clean dump upload user_clean

all: $(OUTPUT_DIR) firmware.elf firmware.bin
	@echo ""
	@echo "=== Runtime A build complete ==="
	@$(TRGT)size firmware.elf
	@echo "Binary: firmware.bin  (flash to 0x00000000)"

# -----------------------------------------------------------------------
# UserLogic B：委托给子目录 Makefile
# -----------------------------------------------------------------------
user:
	$(MAKE) -C user_logic all

user_clean:
	$(MAKE) -C user_logic clean

# -----------------------------------------------------------------------
# 创建输出目录（含子目录 app/）
# -----------------------------------------------------------------------
$(OUTPUT_DIR):
	@mkdir -p $(OUTPUT_DIR)/app

# 通用编译规则（匹配 build/xxx.o 和 build/app/xxx.o）
$(OUTPUT_DIR)/%.o: %.c | $(OUTPUT_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# -----------------------------------------------------------------------
# 链接
# -----------------------------------------------------------------------
firmware.elf: $(OBJECT_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

# elf → bin
%.bin: %.elf
	$(CP) -O binary $< $@

# -----------------------------------------------------------------------
# 工具目标
# -----------------------------------------------------------------------
upload: firmware.bin
	./lpc21isp/lpc21isp -control -donotstart -bin $< $(TTY) 115200 0

dump: firmware.elf
	$(DUMP) -d $< > firmware.s

clean:
	@rm -f *.o *.elf *.bin *.s
	@rm -rf $(OUTPUT_DIR)
