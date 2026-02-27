/*
 * main.c — TiZi PLC Runtime A (宿主固件)
 *
 * 功能：
 *   - 硬件初始化（GPIO、UART、SysTick）
 *   - PLC 周期扫描（默认 10ms 一次）
 *   - 通过 UART 接收上位机的下载/控制命令
 *   - 加载并调用 B 区（USER_FLASH_BASE）的用户逻辑
 *
 * 构建模式（由 Makefile 的 MODE 变量注入宏）：
 *   NCC_MODE=1    默认。B 区为原生 ARM 固件，通过 UserLogic_t 接口表调用。
 *   XCODE_MODE=1  B 区为 WASM 字节码，由内嵌 WAMR 加载并执行。
 *
 * 内存分区：
 *   Runtime A: Flash 0x00000000 (16KB), RAM 0x10000000 (4KB)
 *   UserLogic B: Flash 0x00004000 (16KB), RAM 0x10001000 (4KB)
 */

#include "bsp/lpc_chip/board.h"
#include "bsp/lpc_chip/iocon_8xx.h"
#include "shared_interface.h"

/* -----------------------------------------------------------------------
 * 配置
 * -----------------------------------------------------------------------*/
#define TICKRATE_HZ         1000u  /* SysTick 频率：1kHz → 1ms 分辨率 */
#define DEFAULT_SCAN_MS     10u    /* 默认扫描周期 10ms */

/* -----------------------------------------------------------------------
 * 共享状态（runtime.c 也访问这些变量）
 * -----------------------------------------------------------------------*/
volatile bool     plc_running       = false;
volatile uint32_t plc_scan_time_us  = 0u;
volatile uint8_t  plc_do_state      = 0u;

/* -----------------------------------------------------------------------
 * 内部状态
 * -----------------------------------------------------------------------*/
static volatile uint32_t s_tick_ms    = 0u;
static volatile bool     s_scan_flag  = false;
static uint32_t          s_scan_ms    = DEFAULT_SCAN_MS;

/* -----------------------------------------------------------------------
 * SysTick 中断处理
 * -----------------------------------------------------------------------*/
void SysTick_Handler(void)
{
    s_tick_ms++;
    if ((s_tick_ms % s_scan_ms) == 0u) {
        s_scan_flag = true;
    }
}

/* -----------------------------------------------------------------------
 * System API 实现（提供给 UserLogic B 调用）
 * -----------------------------------------------------------------------*/
static uint32_t sapi_get_tick_ms(void)
{
    return s_tick_ms;
}

static void sapi_uart_puts(const char *s)
{
    Board_UARTPutSTR(s);
}

static void sapi_set_do(uint8_t idx, bool val)
{
    if (idx >= PLC_DO_COUNT) { return; }
    Chip_GPIO_SetPinState(LPC_GPIO_PORT, 0u, (uint8_t)(PLC_DO_BASE_PIN + idx), val);
    if (val) {
        plc_do_state |=  (uint8_t)(1u << idx);
    } else {
        plc_do_state &= (uint8_t)~(1u << idx);
    }
}

static bool sapi_get_di(uint8_t idx)
{
    if (idx >= PLC_DI_COUNT) { return false; }
    return Chip_GPIO_GetPinState(LPC_GPIO_PORT, 0u, (uint8_t)(PLC_DI_BASE_PIN + idx));
}

static const SystemAPI_t s_sapi = {
    .get_tick_ms = sapi_get_tick_ms,
    .uart_puts   = sapi_uart_puts,
    .set_do      = sapi_set_do,
    .get_di      = sapi_get_di,
};

/* -----------------------------------------------------------------------
 * 声明（runtime.c 中实现）
 * -----------------------------------------------------------------------*/
void Runtime_HandleUARTByte(uint8_t byte);

/* -----------------------------------------------------------------------
 * XCODE 模式：xcode_runner.c 中实现
 * -----------------------------------------------------------------------*/
#if defined(XCODE_MODE)
bool xcode_runner_init(const SystemAPI_t *api);
void xcode_runner_loop(uint32_t tick_ms);
#endif

/* -----------------------------------------------------------------------
 * PLC GPIO 初始化
 * -----------------------------------------------------------------------*/
static void plc_gpio_init(void)
{
    /* DI 引脚：输入 + 下拉 */
    for (uint8_t i = 0u; i < PLC_DI_COUNT; i++) {
        uint8_t pin = (uint8_t)(PLC_DI_BASE_PIN + i);
        Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, 0u, pin);
        /* 使用 IOCON 设置下拉（防悬空）*/
        Chip_IOCON_PinSetMode(LPC_IOCON, (CHIP_PINx_T)(IOCON_PIO16 + i), PIN_MODE_PULLDN);
    }

    /* DO 引脚：输出，默认低电平 */
    for (uint8_t i = 0u; i < PLC_DO_COUNT; i++) {
        uint8_t pin = (uint8_t)(PLC_DO_BASE_PIN + i);
        Chip_GPIO_SetPinState(LPC_GPIO_PORT, 0u, pin, false);
        Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, 0u, pin);
    }
}

/* -----------------------------------------------------------------------
 * 安全关闭所有输出（用于停止状态）
 * -----------------------------------------------------------------------*/
static void plc_outputs_clear(void)
{
    for (uint8_t i = 0u; i < PLC_DO_COUNT; i++) {
        sapi_set_do(i, false);
    }
}

/* -----------------------------------------------------------------------
 * 打印十进制数（不使用 printf/snprintf）
 * -----------------------------------------------------------------------*/
static void uart_put_u32(uint32_t val)
{
    char buf[12];
    int  idx = 11;
    buf[idx] = '\0';
    if (val == 0u) {
        Board_UARTPutChar('0');
        return;
    }
    while (val > 0u) {
        buf[--idx] = (char)('0' + (val % 10u));
        val /= 10u;
    }
    Board_UARTPutSTR(&buf[idx]);
}

/* -----------------------------------------------------------------------
 * 主函数
 * -----------------------------------------------------------------------*/
int main(void)
{
    SystemCoreClockUpdate();
    Board_Init();
    plc_gpio_init();

    Board_UARTPutSTR("\r\n=== TiZi PLC Runtime v1.0 ===\r\n");
    Board_UARTPutSTR("build: " __DATE__ " " __TIME__ "\r\n");
    Board_UARTPutSTR("Flash A: 0x00000000 (16KB)  RAM A: 0x10000000 (4KB)\r\n");
    Board_UARTPutSTR("Flash B: 0x00004000 (16KB)  RAM B: 0x10001000 (4KB)\r\n");

#if defined(XCODE_MODE)
    /* ---- XCODE 模式：加载 B 区 .wasm，通过 WAMR 执行 ---- */
    Board_UARTPutSTR("Mode: XCODE (WASM/WAMR)\r\n");
    if (xcode_runner_init(&s_sapi)) {
        plc_running = true;
        Board_UARTPutSTR("WASM PLC started. Scan period: ");
        uart_put_u32(s_scan_ms);
        Board_UARTPutSTR(" ms\r\n");
    } else {
        Board_UARTPutSTR("No valid WASM in Flash B.\r\n");
        Board_UARTPutSTR("Waiting for download via UART...\r\n");
    }
#else
    /* ---- NCC 模式（默认）：读取 B 区原生 UserLogic_t 接口表 ---- */
    Board_UARTPutSTR("Mode: NCC (native)\r\n");
    const UserLogic_t *user = (const UserLogic_t *)USER_FLASH_BASE;

    if (user->magic == USER_LOGIC_MAGIC) {
        Board_UARTPutSTR("UserLogic found: version=");
        uart_put_u32(user->version);
        Board_UARTPutSTR("  DI=");
        uart_put_u32(user->di_count);
        Board_UARTPutSTR("  DO=");
        uart_put_u32(user->do_count);
        Board_UARTPutSTR("\r\n");

        /* 若用户逻辑指定了扫描周期，使用它 */
        if (user->scan_ms > 0u) {
            s_scan_ms = user->scan_ms;
        }

        /* 调用用户初始化，传入 System API 表 */
        user->setup(&s_sapi);

        plc_running = true;
        Board_UARTPutSTR("PLC started. Scan period: ");
        uart_put_u32(s_scan_ms);
        Board_UARTPutSTR(" ms\r\n");
    } else {
        Board_UARTPutSTR("No UserLogic (magic mismatch).\r\n");
        Board_UARTPutSTR("Waiting for download via UART...\r\n");
    }
#endif

    /* --- 启动 SysTick --- */
    SysTick_Config(SystemCoreClock / TICKRATE_HZ);

    /* --- 主循环 --- */
    while (1) {
        /* 轮询 UART，将字节交给下载协议状态机 */
        int ch = Board_UARTGetChar();
        if (ch != -1) {
            Runtime_HandleUARTByte((uint8_t)ch);
        }

        /* PLC 周期扫描 */
        if (s_scan_flag) {
            s_scan_flag = false;

            if (plc_running) {
                uint32_t t0 = s_tick_ms;

#if defined(XCODE_MODE)
                /* XCODE 模式：通过 WAMR 执行 plc_run(ms) */
                xcode_runner_loop(s_tick_ms);
#else
                /* NCC 模式：调用原生用户逻辑 */
                if (user->magic == USER_LOGIC_MAGIC) {
                    user->loop();
                }
#endif
                /* 记录本次扫描耗时（近似，单位 ms，*1000 得 us） */
                plc_scan_time_us = (s_tick_ms - t0) * 1000u;
            } else {
                /* 停止状态：确保所有输出安全关闭 */
                plc_outputs_clear();
            }
        }
    }
}
