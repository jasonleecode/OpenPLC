# Makefile — UserLogic B (用户逻辑 / PLC 程序)
#
# 生成 user_logic.bin，由上位机通过 UART 协议下载到 Flash B 区（0x00004000）。
# 不包含启动文件，不需要 main()；接口表由 user_logic.c 中的 .user_header 段提供。

OUTPUT_DIR := build

# ARM Cortex-M0+ 交叉编译器
TRGT = arm-none-eabi-
CC   = $(TRGT)gcc
CP   = $(TRGT)objcopy
DUMP = $(TRGT)objdump

# 编译选项（和 Runtime A 保持一致）
CFLAGS  = -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
CFLAGS += -I../ -I../bsp/include
CFLAGS += -Os -ggdb
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -Wall -Wextra

# 链接选项：
#   -nostartfiles   不链接 C 运行时启动代码（由 setup() 手动初始化 RAM）
#   -nolibc         不链接标准库
#   -Wl,--gc-sections  去除未使用的段，减小 bin 体积
LDFLAGS  = -Wl,--script=lpc824_user.ld
LDFLAGS += -nostartfiles -nolibc -mfloat-abi=soft
LDFLAGS += -Wl,--gc-sections

SOURCE_FILES := user_logic.c

OBJECT_FILES := $(patsubst %.c, $(OUTPUT_DIR)/%.o, $(SOURCE_FILES))

.PHONY: all clean dump

all: $(OUTPUT_DIR) user_logic.elf user_logic.bin
	@echo ""
	@echo "=== UserLogic B build complete ==="
	@$(TRGT)size user_logic.elf
	@echo "Binary: user_logic.bin  (download this to Flash B @ 0x00004000)"

$(OUTPUT_DIR):
	@mkdir -p $(OUTPUT_DIR)

$(OUTPUT_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

user_logic.elf: $(OBJECT_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

%.bin: %.elf
	$(CP) -O binary $< $@

dump: user_logic.elf
	$(DUMP) -d $< > user_logic.s

clean:
	@rm -f *.o *.elf *.bin *.s
	@rm -rf $(OUTPUT_DIR)
