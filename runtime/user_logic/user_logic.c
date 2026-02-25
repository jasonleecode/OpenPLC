/*
 * user_logic.c — UserLogic B 模板
 *
 * 上位机（TiZi 编辑器）根据梯形图/ST 生成的代码替换此文件的 loop() 函数体。
 *
 * 约束：
 *   1. user_api 结构体必须用 __attribute__((section(".user_header"))) 修饰，
 *      链接脚本会确保它位于 Flash B 起始地址（0x00004000）。
 *   2. setup() 中保存好 System API 指针后即可使用所有系统服务。
 *   3. loop() 每个扫描周期调用一次，不要在里面死循环或长时间阻塞。
 *   4. 不要使用标准库（printf 等），只通过 g_api->uart_puts() 输出。
 *   5. 全局变量会保留在 RAM B 区（0x10001000），由 setup() 负责初始化。
 */

#include "../shared_interface.h"

/* -----------------------------------------------------------------------
 * B 区 RAM 初始化
 * 由于没有 C 启动代码，setup() 必须手动把 .data 段从 Flash 搬到 RAM，
 * 并把 .bss 段清零。链接脚本提供了以下符号：
 *   _etext_b  = Flash 中 .data 初始值的起始地址
 *   _data_b   = RAM 中 .data 的目标起始地址
 *   _edata_b  = RAM 中 .data 的目标结束地址
 *   _bss_b    = RAM 中 .bss 起始
 *   _ebss_b   = RAM 中 .bss 结束
 * -----------------------------------------------------------------------*/
extern uint32_t _etext_b;
extern uint32_t _data_b;
extern uint32_t _edata_b;
extern uint32_t _bss_b;
extern uint32_t _ebss_b;

static void user_ram_init(void)
{
    /* 搬运 .data 段初始值 */
    uint32_t *src = &_etext_b;
    uint32_t *dst = &_data_b;
    while (dst < &_edata_b) {
        *dst++ = *src++;
    }
    /* 清零 .bss 段 */
    dst = &_bss_b;
    while (dst < &_ebss_b) {
        *dst++ = 0u;
    }
}

/* -----------------------------------------------------------------------
 * 用户逻辑状态（可在 loop() 中跨周期保持）
 * -----------------------------------------------------------------------*/
static const SystemAPI_t *g_api = 0;

/* 示例：用于计数的全局变量（保留在 RAM B，跨扫描周期）*/
static uint32_t s_scan_count = 0u;

/* -----------------------------------------------------------------------
 * setup() — 上电或重新下载后调用一次
 * -----------------------------------------------------------------------*/
static void setup(const SystemAPI_t *api)
{
    user_ram_init();   /* 先初始化 B 区 RAM */
    g_api = api;
    g_api->uart_puts("UserLogic B: setup OK\r\n");
}

/* -----------------------------------------------------------------------
 * loop() — 每个扫描周期调用一次（由 Runtime A 的 SysTick 驱动）
 *
 * 此处是上位机生成代码的替换目标。
 * 默认实现：DI 直通 DO（数字输入透传到同编号的数字输出）
 * -----------------------------------------------------------------------*/
static void loop(void)
{
    s_scan_count++;

    /* ---- 用户逻辑开始 ---- */

    /* 示例：DI0-DI3 直通 DO0-DO3 */
    for (uint8_t i = 0u; i < PLC_DI_COUNT && i < PLC_DO_COUNT; i++) {
        bool di_val = g_api->get_di(i);
        g_api->set_do(i, di_val);
    }

    /* ---- 用户逻辑结束 ---- */
}

/* -----------------------------------------------------------------------
 * 接口表 — 必须使用 .user_header 段，确保位于 Flash B 起始地址
 * -----------------------------------------------------------------------*/
const UserLogic_t user_api __attribute__((section(".user_header"))) = {
    .magic    = USER_LOGIC_MAGIC,
    .version  = USER_LOGIC_VERSION,
    .setup    = setup,
    .loop     = loop,
    .di_count = PLC_DI_COUNT,
    .do_count = PLC_DO_COUNT,
    .scan_ms  = 0u,   /* 0 = 使用 Runtime A 的默认扫描周期 */
};
