# Makefile — TiZi PLC Runtime A (宿主固件)
#
# 构建目标：firmware.bin → 烧录到 Flash A (0x00000000, 16KB)
#
# 构建模式（MODE 变量）：
#   MODE=NCC    (默认) Native Code Compiler — B 区为 arm-none-eabi-gcc 编译的原生 ARM 固件
#   MODE=XCODE          XCODE —— B 区为 WASM 字节码，Runtime A 内嵌 WAMR 解释执行
#
# 示例：
#   make                    → NCC 模式
#   make MODE=XCODE         → XCODE 模式（需要 WAMR 库已构建）
#   make user               → 构建 UserLogic B（NCC）
#   make user MODE=XCODE    → 构建 UserLogic B（XCODE，调用 editor 的 WASM 编译流程）

# -----------------------------------------------------------------------
# 构建模式选择
# -----------------------------------------------------------------------
MODE ?= NCC

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

# WAMR 仓库路径（XCODE 模式使用；可通过命令行覆盖）
WAMR_ROOT ?= ../library/wasm

# -----------------------------------------------------------------------
# 编译选项（公共）
# -----------------------------------------------------------------------
CFLAGS   = -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
CFLAGS  += -I./ -I./bsp -I./bsp/include -I./bsp/lpc_chip
CFLAGS  += -Os -ggdb
CFLAGS  += -ffunction-sections -fdata-sections
CFLAGS  += -Wall -Wextra
CXXFLAGS = $(CFLAGS) -fno-rtti -fno-exceptions

# 链接选项
LDFLAGS  = -Wl,--script=./bsp/include/lpc824.ld
LDFLAGS += -nostartfiles -mfloat-abi=soft -nolibc
LDFLAGS += -Wl,--gc-sections

# vpath 让 make 在这些目录里查找 .c / .h
vpath %.c bsp/lpc_chip app
vpath %.h bsp/lpc_chip bsp/include app

# -----------------------------------------------------------------------
# NCC 模式（默认）：原生 ARM 代码，UserLogic_t 接口表
# -----------------------------------------------------------------------
ifeq ($(MODE), NCC)
    CFLAGS += -DNCC_MODE=1
    $(info [Mode] NCC — UserLogic B is native ARM code.)
endif

# -----------------------------------------------------------------------
# XCODE 模式：内嵌 WAMR，B 区存放 .wasm 字节码
# -----------------------------------------------------------------------
ifeq ($(MODE), XCODE)
    CFLAGS += -DXCODE_MODE=1

    # WAMR 解释器（纯解释模式，不使用 AOT/JIT）
    CFLAGS += -DWASM_ENABLE_INTERP=1
    CFLAGS += -DWASM_ENABLE_AOT=0
    CFLAGS += -DWASM_ENABLE_FAST_JIT=0
    CFLAGS += -DWASM_ENABLE_JIT=0
    CFLAGS += -DWASM_ENABLE_LIBC_BUILTIN=0
    CFLAGS += -DWASM_ENABLE_LIBC_WASI=0
    CFLAGS += -DWASM_ENABLE_MULTI_MODULE=0
    CFLAGS += -DWASM_ENABLE_THREAD_MGR=0

    # WAMR 头文件路径
    CFLAGS += -I$(WAMR_ROOT)/core/iwasm/include
    CFLAGS += -I$(WAMR_ROOT)/core/shared/include
    CFLAGS += -I$(WAMR_ROOT)/core/shared/platform/include
    CFLAGS += -I$(WAMR_ROOT)/core/shared/mem-alloc

    # WAMR 核心源文件（解释模式最小集合）
    WAMR_SOURCES := \
        $(WAMR_ROOT)/core/iwasm/interpreter/wasm_interp_classic.c   \
        $(WAMR_ROOT)/core/iwasm/interpreter/wasm_loader.c            \
        $(WAMR_ROOT)/core/iwasm/common/wasm_runtime_common.c         \
        $(WAMR_ROOT)/core/iwasm/common/wasm_native.c                 \
        $(WAMR_ROOT)/core/iwasm/common/wasm_exec_env.c               \
        $(WAMR_ROOT)/core/iwasm/common/wasm_memory.c                 \
        $(WAMR_ROOT)/core/shared/mem-alloc/ems/ems_alloc.c           \
        $(WAMR_ROOT)/core/shared/mem-alloc/ems/ems_hmu.c             \
        $(WAMR_ROOT)/core/shared/mem-alloc/ems/ems_kfc.c             \
        $(WAMR_ROOT)/core/shared/mem-alloc/mem_alloc.c               \
        $(WAMR_ROOT)/core/shared/utils/bh_common.c                   \
        $(WAMR_ROOT)/core/shared/utils/bh_log.c                      \
        $(WAMR_ROOT)/core/shared/utils/bh_list.c                     \
        $(WAMR_ROOT)/core/shared/utils/bh_vector.c

    # XCODE 专用运行器（WAMR 初始化 + plc_init/plc_run 调用）
    XCODE_SOURCES := app/xcode_runner.c

    $(info [Mode] XCODE — UserLogic B is WASM bytecode, executed via WAMR.)
    $(info [WAMR] $(WAMR_ROOT))
endif

# -----------------------------------------------------------------------
# Runtime A 源文件列表（两种模式共用）
# -----------------------------------------------------------------------
SOURCE_FILES := \
    app/main.c        \
    app/libutil.c     \
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

# XCODE 模式追加 WAMR 源文件和 xcode_runner
ifeq ($(MODE), XCODE)
    SOURCE_FILES += $(WAMR_SOURCES) $(XCODE_SOURCES)
endif

# 目标文件列表（保留子目录路径）
OBJECT_FILES := $(addprefix $(OUTPUT_DIR)/, $(patsubst %.c, %.o, $(SOURCE_FILES)))

# -----------------------------------------------------------------------
# 默认目标：构建 Runtime A
# -----------------------------------------------------------------------
.PHONY: all user clean dump upload user_clean

all: $(OUTPUT_DIR) firmware.elf firmware.bin
	@echo ""
	@echo "=== Runtime A build complete [MODE=$(MODE)] ==="
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
