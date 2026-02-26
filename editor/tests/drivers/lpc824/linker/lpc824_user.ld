/*
 * lpc824_user.ld — UserLogic B 链接脚本
 *
 * 关键约束：
 *   .user_header 段必须位于 0x00004000 的最开头，
 *   这样 Runtime A 才能通过固定地址找到 UserLogic_t 接口表。
 *
 * Flash B: 0x00004000 ~ 0x00007FFF  (16KB, 扇区 16-31)
 * RAM B  : 0x10001000 ~ 0x10001FFF  (4KB)
 */

MEMORY
{
    flash_b : org = 0x00004000, len = 16K
    ram_b   : org = 0x10001000, len = 4K
}

__stack_size__ = 256;

/*
 * UserLogic B 没有自己的 main，由 Runtime A 通过函数指针调用。
 * 不需要 ENTRY()，但 .user_header 必须首先出现。
 */

SECTIONS
{
    /*
     * 接口表固定在 Flash B 起始位置 (0x00004000)
     * 对应 C 代码中：
     *   const UserLogic_t user_api __attribute__((section(".user_header"))) = {...};
     */
    .user_header :
    {
        . = ALIGN(4);
        KEEP(*(.user_header))
        . = ALIGN(4);
    } > flash_b

    .text :
    {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext_b = .;
    } > flash_b

    .data :
    {
        . = ALIGN(4);
        _data_b = .;
        *(.data*)
        . = ALIGN(4);
        _edata_b = .;
    } > ram_b AT > flash_b

    .bss :
    {
        _bss_b = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss_b = .;
    } > ram_b

    PROVIDE(_pvHeapStart_b = _ebss_b);
    PROVIDE(_vStackTop_b   = ORIGIN(ram_b) + LENGTH(ram_b));
}
