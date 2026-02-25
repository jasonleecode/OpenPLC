/*
 * lpc824.ld — Runtime A (宿主固件) 链接脚本
 *
 * Flash 分区（LPC824 共 32KB, 每扇区 1KB, 共 32 扇区）:
 *   Runtime A  : 0x00000000 ~ 0x00003FFF  16KB  扇区 0-15
 *   UserLogic B: 0x00004000 ~ 0x00007FFF  16KB  扇区 16-31  ← 上位机下载
 *
 * RAM 分区（共 8KB）:
 *   Runtime A  : 0x10000000 ~ 0x10000FFF  4KB
 *   UserLogic B: 0x10001000 ~ 0x10001FFF  4KB
 */

MEMORY
{
    flash_a : org = 0x00000000, len = 16K   /* Runtime A 独占 */
    ram_a   : org = 0x10000000, len = 4K    /* Runtime A 独占 */
}

/* 栈大小 */
__stack_size__ = 512;

ENTRY(ResetISR)

SECTIONS
{
    .text :
    {
        KEEP(*(.isr_vector))
        *(.after_vectors*)
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);

        PROVIDE(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__init_array_end = .);

        . = ALIGN(4);
        _etext = .;
    } > flash_a

    .data :
    {
        . = ALIGN(4);
        _data = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > ram_a AT > flash_a

    .bss :
    {
        _bss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        PROVIDE(end = .);
    } > ram_a

    PROVIDE(_pvHeapStart = _ebss);
    PROVIDE(_pvHeapLimit = ORIGIN(ram_a) + LENGTH(ram_a) - __stack_size__);
    PROVIDE(_vStackTop   = ORIGIN(ram_a) + LENGTH(ram_a));
}
