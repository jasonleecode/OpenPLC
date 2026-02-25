MEMORY
{
  flash : org = 0x00000000, len = 32k
  ram : org = 0x10000000, len = 8k
}

/* 堆栈大小定义 (min: 256 bytes) */
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
    } > flash

    .data :
    {
        . = ALIGN(4) ;
        _data = .;
        *(.data*)
        . = ALIGN(4) ;
        _edata = .;
    } > ram AT > flash

    .bss :
    {
        _bss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4) ;
        _ebss = .;
        PROVIDE(end = .);
    } > ram

    /* 定义堆的开始 */
    PROVIDE(_pvHeapStart = _ebss);
    
    /* 定义堆的结尾（为栈留出空间）*/
    PROVIDE(_pvHeapLimit = ORIGIN(ram) + LENGTH(ram) - __stack_size__);
    
    /* 定义栈顶 */
    PROVIDE(_vStackTop = ORIGIN(ram) + LENGTH(ram));
}
