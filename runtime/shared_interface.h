/*
 * shared_interface.h — A/B 分区共享协议
 *
 * Runtime (A) 和 UserLogic (B) 共同引用此文件。
 * 它定义了两者通信的"合同"：跳转表结构体和内存映射。
 */

#ifndef SHARED_INTERFACE_H
#define SHARED_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Flash / RAM 分区地址
 * LPC824: Flash 32KB @ 0x00000000, SRAM 8KB @ 0x10000000
 * 每个 Flash 扇区 1KB，共 32 个扇区 (0-31)
 * -----------------------------------------------------------------------*/
#define RUNTIME_FLASH_BASE       0x00000000U   /* Runtime A: 扇区 0-15,  16KB */
#define RUNTIME_FLASH_SIZE       (16u * 1024u)
#define USER_FLASH_BASE          0x00004000U   /* UserLogic B: 扇区 16-31, 16KB */
#define USER_FLASH_SIZE          (16u * 1024u)

#define RUNTIME_RAM_BASE         0x10000000U   /* Runtime A 专用 RAM, 4KB */
#define RUNTIME_RAM_SIZE         (4u * 1024u)
#define USER_RAM_BASE            0x10001000U   /* UserLogic B 专用 RAM, 4KB */
#define USER_RAM_SIZE            (4u * 1024u)

/* LPC824 Flash 扇区号 (1KB/sector) */
#define USER_FLASH_SECTOR_START  16u
#define USER_FLASH_SECTOR_END    31u

/* -----------------------------------------------------------------------
 * 接口版本与魔数
 * -----------------------------------------------------------------------*/
#define USER_LOGIC_MAGIC    0xDEADBEEFu
#define USER_LOGIC_VERSION  1u

/* -----------------------------------------------------------------------
 * PLC I/O 配置
 * DI0-DI3: PIO0_16 ~ PIO0_19  (输入)
 * DO0-DO3: PIO0_12 ~ PIO0_15  (输出)
 * -----------------------------------------------------------------------*/
#define PLC_DI_COUNT  4u
#define PLC_DO_COUNT  4u
#define PLC_DI_BASE_PIN  16u
#define PLC_DO_BASE_PIN  12u

/* -----------------------------------------------------------------------
 * System API — Runtime 提供给 UserLogic 的系统服务函数表
 * UserLogic 只调用这些函数，不链接 Runtime 的任何符号
 * -----------------------------------------------------------------------*/
typedef struct {
    uint32_t (*get_tick_ms)(void);                   /* 系统运行毫秒数 */
    void     (*uart_puts)(const char *s);            /* 输出字符串到 UART */
    void     (*set_do)(uint8_t idx, bool val);       /* 写数字输出 */
    bool     (*get_di)(uint8_t idx);                 /* 读数字输入 */
} SystemAPI_t;

/* -----------------------------------------------------------------------
 * UserLogic 接口表 — 必须放置在 USER_FLASH_BASE (0x00004000) 的最开头
 * -----------------------------------------------------------------------*/
typedef struct {
    uint32_t magic;          /* USER_LOGIC_MAGIC — 校验 B 区有效性 */
    uint32_t version;        /* USER_LOGIC_VERSION */
    void    (*setup)(const SystemAPI_t *api);   /* 初始化，只调用一次 */
    void    (*loop)(void);                       /* 每个扫描周期调用 */
    uint8_t  di_count;       /* 用户逻辑期望的 DI 数量 */
    uint8_t  do_count;       /* 用户逻辑期望的 DO 数量 */
    uint16_t scan_ms;        /* 请求的扫描周期 ms，0 = 使用 Runtime 默认值 */
} UserLogic_t;

#endif /* SHARED_INTERFACE_H */
