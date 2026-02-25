# Minimal gcc makefile for LPC824

# define target folder
OUTPUT_DIR := build
SRC_DIR := lpc_chip lpc21isp

# default Linux USB device name for upload
TTY ?= /dev/ttyACM*

# use the arm cross compiler, not std gcc
TRGT = arm-none-eabi-
CC = $(TRGT)gcc
CXX = $(TRGT)g++
CP = $(TRGT)objcopy
DUMP = $(TRGT)objdump

# compiler and linker settings
CFLAGS = -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -I./include -I./ -Os -ggdb
CXXFLAGS = $(CFLAGS) -fno-rtti -fno-exceptions
LDFLAGS = -Wl,--script=./include/lpc824.ld -nostartfiles -mfloat-abi=soft -nolibc

# permit including this makefile from a sibling directory
vpath %.c lpc_chip:  startup: Include
vpath %.h lpc_chip:  startup: Include

# define source file list
SOURCE_FILES := main.c libutil.c gcc_startup_lpc82x.c board_sysinit.c board.c \
 board_api_stubs_8xx.c crp.c mtb.c sysinit.c acmp_8xx.c adc_8xx.c \
 board.c board_sysinit.c chip_8xx.c clock_8xx.c crc_8xx.c dma_8xx.c \
 eeprom.c gpio_8xx.c i2c_common_8xx.c i2cm_8xx.c i2cs_8xx.c iap.c \
 iocon_8xx.c irc_8xx.c pinint_8xx.c pmu_8xx.c ring_buffer.c rtc_ut.c \
 sct_8xx.c sct_pwm_8xx.c spi_8xx.c spim_8xx.c spis_8xx.c stopwatch_8xx.c \
 swm_8xx.c syscon_8xx.c sysinit_8xx.c uart_8xx.c wkt_8xx.c wwdt_8xx.c
#SOURCE_FILES := $(wildcard $(SRC_DIR)/*.c)

# define target file list
OBJECT_FILES := $(patsubst %.c, $(OUTPUT_DIR)/%.o, $(SOURCE_FILES))

# default target, build all
all: $(OUTPUT_DIR) firmware.elf firmware.bin

# create build folder
$(OUTPUT_DIR):
	@mkdir -p $(OUTPUT_DIR)

# build every source file to target
$(OUTPUT_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# generate execu file
firmware.elf: $(OBJECT_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

# convert elf to binary
%.bin: %.elf
	$(CP) -O binary $< $@

# default target
upload: firmware.bin
	./lpc21isp/lpc21isp -control -donotstart -bin $< $(TTY) 115200 0

dump: firmware.elf
	$(DUMP) -d $< > firmware.s

clean:
	@rm -f *.o *.elf *.bin *.s
	@rm -rf $(OUTPUT_DIR)

# these target names don't represent real files
.PHONY: upload dump clean
