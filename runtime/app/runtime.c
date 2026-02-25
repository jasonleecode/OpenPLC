/*
 * app/runtime.c — PLC Runtime 核心
 *
 * 负责：
 *   1. UART 下载协议状态机（接收上位机发来的用户逻辑 .bin）
 *   2. IAP Flash 编程（将接收到的数据写入 B 区 Flash）
 *   3. 对外暴露 Runtime_HandleUARTByte() 供 main.c 调用
 *
 * 协议帧格式：
 *   [SOF:1][CMD:1][LEN_LO:1][LEN_HI:1][DATA:LEN][CRC8:1]
 *   SOF = 0xAA
 *
 * 命令列表：
 *   0x01 PING        → 回复 "TiZi" 版本串
 *   0x02 ERASE       → 擦除 B 区全部扇区 (16-31)
 *   0x03 WRITE_PAGE  → 写 256 字节到 Flash，载荷 = [addr:4LE][data:256]
 *   0x04 VERIFY      → CRC 校验，载荷 = [addr:4LE][len:2LE][crc8:1]
 *   0x05 RESET       → 软复位，重新加载用户逻辑
 *   0x10 GET_STATUS  → 获取 PLC 状态
 *   0x11 SET_RUN     → 启动/停止 PLC 扫描
 *   0x12 READ_IO     → 读当前 DI/DO 状态
 *
 * 响应：
 *   成功 → ACK (0x06) 或完整响应帧
 *   失败 → NAK (0x15)
 */

#include "bsp/lpc_chip/board.h"
#include "bsp/lpc_chip/iap.h"
#include "shared_interface.h"

/* -----------------------------------------------------------------------
 * 协议常量
 * -----------------------------------------------------------------------*/
#define PROTO_SOF        0xAAu
#define ACK              0x06u
#define NAK              0x15u

#define CMD_PING         0x01u
#define CMD_ERASE        0x02u
#define CMD_WRITE_PAGE   0x03u
#define CMD_VERIFY       0x04u
#define CMD_RESET        0x05u
#define CMD_GET_STATUS   0x10u
#define CMD_SET_RUN      0x11u
#define CMD_READ_IO      0x12u

/* IAP 写入/擦除要求的最小单元 */
#define FLASH_PAGE_SIZE  256u   /* IAP CopyRamToFlash 最小 256 字节 */
#define FLASH_SECTOR_SIZE 1024u /* LPC824 每扇区 1KB */

/* -----------------------------------------------------------------------
 * 解析状态机
 * -----------------------------------------------------------------------*/
typedef enum {
    PARSE_SOF,
    PARSE_CMD,
    PARSE_LEN_LO,
    PARSE_LEN_HI,
    PARSE_DATA,
    PARSE_CRC,
} ParseState_t;

/* 最大载荷：4字节地址 + 256字节数据 */
#define RX_BUF_SIZE  (4u + FLASH_PAGE_SIZE + 4u)

static ParseState_t s_state   = PARSE_SOF;
static uint8_t      s_cmd;
static uint16_t     s_len;
static uint16_t     s_rx_idx;
static uint8_t      s_rx_buf[RX_BUF_SIZE + 1u]; /* +1 存 CRC 字节 */

/* -----------------------------------------------------------------------
 * 外部变量（定义在 main.c）
 * -----------------------------------------------------------------------*/
extern volatile bool     plc_running;
extern volatile uint32_t plc_scan_time_us;
extern volatile uint8_t  plc_do_state;

/* -----------------------------------------------------------------------
 * CRC-8/MAXIM (polynomial 0x31, init 0x00)
 * -----------------------------------------------------------------------*/
static uint8_t crc8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0u;
    for (uint16_t i = 0u; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80u) ? ((crc << 1u) ^ 0x31u) : (crc << 1u);
        }
    }
    return crc;
}

/* -----------------------------------------------------------------------
 * 发送辅助
 * -----------------------------------------------------------------------*/
static void send_ack(void) { Board_UARTPutChar(ACK); }
static void send_nak(void) { Board_UARTPutChar(NAK); }

static void send_response(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    Board_UARTPutChar(PROTO_SOF);
    Board_UARTPutChar(cmd);
    Board_UARTPutChar((uint8_t)(len & 0xFFu));
    Board_UARTPutChar((uint8_t)(len >> 8u));
    for (uint16_t i = 0u; i < len; i++) {
        Board_UARTPutChar(data[i]);
    }
    Board_UARTPutChar(crc8(data, len));
}

/* -----------------------------------------------------------------------
 * IAP Flash 编程辅助
 * -----------------------------------------------------------------------*/
static uint8_t flash_erase_user(void)
{
    uint8_t r;
    __disable_irq();
    r = Chip_IAP_PreSectorForReadWrite(USER_FLASH_SECTOR_START, USER_FLASH_SECTOR_END);
    if (r == IAP_CMD_SUCCESS) {
        r = Chip_IAP_EraseSector(USER_FLASH_SECTOR_START, USER_FLASH_SECTOR_END);
    }
    __enable_irq();
    return r;
}

static uint8_t flash_write_page(uint32_t dst_addr, uint8_t *src, uint32_t size)
{
    if (dst_addr < USER_FLASH_BASE ||
        dst_addr + size > USER_FLASH_BASE + USER_FLASH_SIZE) {
        return IAP_DST_ADDR_NOT_MAPPED;
    }
    uint32_t sec_start = dst_addr / FLASH_SECTOR_SIZE;
    uint32_t sec_end   = (dst_addr + size - 1u) / FLASH_SECTOR_SIZE;

    uint8_t r;
    __disable_irq();
    r = Chip_IAP_PreSectorForReadWrite(sec_start, sec_end);
    if (r == IAP_CMD_SUCCESS) {
        r = Chip_IAP_CopyRamToFlash(dst_addr, (uint32_t *)src, size);
    }
    __enable_irq();
    return r;
}

/* -----------------------------------------------------------------------
 * 命令处理
 * -----------------------------------------------------------------------*/
static void process_command(void)
{
    /* 校验 CRC */
    uint8_t expected = crc8(s_rx_buf, s_len);
    uint8_t actual   = s_rx_buf[s_len]; /* CRC 存在载荷末尾+1 处 */
    if (actual != expected) {
        send_nak();
        return;
    }

    switch (s_cmd) {

    /* ---- PING -------------------------------------------------------- */
    case CMD_PING: {
        uint8_t resp[8] = {'T','i','Z','i',
                           'v', '1', '.', '0'};
        send_response(CMD_PING, resp, 8u);
        break;
    }

    /* ---- ERASE ------------------------------------------------------- */
    case CMD_ERASE: {
        uint8_t r = flash_erase_user();
        if (r == IAP_CMD_SUCCESS) send_ack(); else send_nak();
        break;
    }

    /* ---- WRITE_PAGE -------------------------------------------------- */
    case CMD_WRITE_PAGE: {
        /* 载荷：[addr:4LE][data:256] */
        if (s_len != 4u + FLASH_PAGE_SIZE) {
            send_nak();
            break;
        }
        uint32_t addr = (uint32_t)s_rx_buf[0]
                      | ((uint32_t)s_rx_buf[1] << 8u)
                      | ((uint32_t)s_rx_buf[2] << 16u)
                      | ((uint32_t)s_rx_buf[3] << 24u);

        uint8_t r = flash_write_page(addr, &s_rx_buf[4], FLASH_PAGE_SIZE);
        if (r == IAP_CMD_SUCCESS) send_ack(); else send_nak();
        break;
    }

    /* ---- VERIFY ------------------------------------------------------ */
    case CMD_VERIFY: {
        /* 载荷：[addr:4LE][len:2LE][expected_crc8:1] */
        if (s_len != 7u) {
            send_nak();
            break;
        }
        uint32_t addr  = (uint32_t)s_rx_buf[0]
                       | ((uint32_t)s_rx_buf[1] << 8u)
                       | ((uint32_t)s_rx_buf[2] << 16u)
                       | ((uint32_t)s_rx_buf[3] << 24u);
        uint16_t vlen  = (uint16_t)s_rx_buf[4]
                       | ((uint16_t)s_rx_buf[5] << 8u);
        uint8_t  ecrc  = s_rx_buf[6];
        uint8_t  acrc  = crc8((const uint8_t *)addr, vlen);
        if (acrc == ecrc) send_ack(); else send_nak();
        break;
    }

    /* ---- RESET ------------------------------------------------------- */
    case CMD_RESET: {
        send_ack();
        NVIC_SystemReset();
        break;
    }

    /* ---- GET_STATUS -------------------------------------------------- */
    case CMD_GET_STATUS: {
        uint32_t t = plc_scan_time_us;
        uint8_t resp[5];
        resp[0] = plc_running ? 1u : 0u;
        resp[1] = (uint8_t)(t & 0xFFu);
        resp[2] = (uint8_t)(t >> 8u);
        resp[3] = (uint8_t)(t >> 16u);
        resp[4] = (uint8_t)(t >> 24u);
        send_response(CMD_GET_STATUS, resp, 5u);
        break;
    }

    /* ---- SET_RUN ----------------------------------------------------- */
    case CMD_SET_RUN: {
        if (s_len != 1u) { send_nak(); break; }
        plc_running = (s_rx_buf[0] != 0u);
        send_ack();
        break;
    }

    /* ---- READ_IO ----------------------------------------------------- */
    case CMD_READ_IO: {
        /* resp[0] = DI 位图, resp[1] = DO 位图 */
        uint8_t di_bits = 0u;
        for (uint8_t i = 0u; i < PLC_DI_COUNT; i++) {
            if (Chip_GPIO_GetPinState(LPC_GPIO_PORT, 0, PLC_DI_BASE_PIN + i)) {
                di_bits |= (uint8_t)(1u << i);
            }
        }
        uint8_t resp[2] = { di_bits, plc_do_state };
        send_response(CMD_READ_IO, resp, 2u);
        break;
    }

    default:
        send_nak();
        break;
    }
}

/* -----------------------------------------------------------------------
 * 公开接口：main.c 每收到一个 UART 字节调用此函数
 * -----------------------------------------------------------------------*/
void Runtime_HandleUARTByte(uint8_t byte)
{
    switch (s_state) {

    case PARSE_SOF:
        if (byte == PROTO_SOF) { s_state = PARSE_CMD; }
        break;

    case PARSE_CMD:
        s_cmd   = byte;
        s_state = PARSE_LEN_LO;
        break;

    case PARSE_LEN_LO:
        s_len   = (uint16_t)byte;
        s_state = PARSE_LEN_HI;
        break;

    case PARSE_LEN_HI:
        s_len  |= ((uint16_t)byte << 8u);
        s_rx_idx = 0u;
        if (s_len > (uint16_t)(RX_BUF_SIZE)) {
            s_state = PARSE_SOF; /* 载荷超长，丢弃 */
        } else if (s_len == 0u) {
            s_state = PARSE_CRC;
        } else {
            s_state = PARSE_DATA;
        }
        break;

    case PARSE_DATA:
        s_rx_buf[s_rx_idx++] = byte;
        if (s_rx_idx >= s_len) { s_state = PARSE_CRC; }
        break;

    case PARSE_CRC:
        s_rx_buf[s_len] = byte; /* 存 CRC 在载荷末尾 */
        process_command();
        s_state = PARSE_SOF;
        break;
    }
}
