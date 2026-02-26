#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <cstdint>

class IPlcTransport;
class QTimer;

// ─────────────────────────────────────────────────────────────────────────────
// PlcProtocol — TiZi Runtime 下载协议
//
// 协议帧格式（与 runtime/app/runtime.c 完全对应）：
//   [SOF:0xAA][CMD:1][LEN_LO:1][LEN_HI:1][DATA:LEN][CRC8:1]
//
// 响应：
//   ACK  (0x06) — 单字节，命令成功
//   NAK  (0x15) — 单字节，命令失败
//   完整帧 — PING / GET_STATUS / READ_IO 的响应
//
// 下载流程：PING → ERASE → WRITE_PAGE×N → VERIFY → RESET
// ─────────────────────────────────────────────────────────────────────────────
class PlcProtocol : public QObject {
    Q_OBJECT
public:
    // 与 runtime/shared_interface.h 保持一致
    static constexpr uint32_t USER_FLASH_BASE = 0x00004000u;
    static constexpr uint32_t FLASH_PAGE_SIZE = 256u;

    // 命令码
    static constexpr uint8_t CMD_PING        = 0x01;
    static constexpr uint8_t CMD_ERASE       = 0x02;
    static constexpr uint8_t CMD_WRITE_PAGE  = 0x03;
    static constexpr uint8_t CMD_VERIFY      = 0x04;
    static constexpr uint8_t CMD_RESET       = 0x05;
    static constexpr uint8_t CMD_GET_STATUS  = 0x10;
    static constexpr uint8_t CMD_SET_RUN     = 0x11;
    static constexpr uint8_t CMD_READ_IO     = 0x12;

    explicit PlcProtocol(IPlcTransport* transport, QObject* parent = nullptr);

    // ── 高层操作 ──────────────────────────────────────────────
    // 下载二进制到 Flash B 区，自动完成 PING/ERASE/WRITE/VERIFY/RESET
    void downloadBinary(const QByteArray& bin);
    void abort();

    // ── 单独命令（下载之外的运行时控制）──────────────────────
    void sendPing();
    void sendGetStatus();
    void sendSetRun(bool run);
    void sendReadIo();

signals:
    void pingResponse(const QString& version);
    void statusResponse(bool running, uint32_t scanTimeUs);
    void ioResponse(uint8_t diBits, uint8_t doBits);

    // 下载进度
    void downloadProgress(int page, int totalPages);
    void downloadComplete();
    void downloadFailed(const QString& reason);

    // 日志（供 DownloadDialog 显示）
    void logMessage(const QString& msg);

private:
    // ── 响应帧解析状态机 ──────────────────────────────────────
    enum class ParseState {
        WaitFirst,   // 等待 ACK / NAK / SOF
        FrameCmd,
        FrameLenLo,
        FrameLenHi,
        FrameData,
        FrameCrc,
    };

    // ── 下载流程步骤 ──────────────────────────────────────────
    enum class DlStep {
        Idle, Ping, Erase, Write, Verify, Reset
    };

    IPlcTransport* m_transport;
    QTimer*        m_timeoutTimer;

    // 解析状态
    ParseState m_parseState = ParseState::WaitFirst;
    uint8_t    m_frameCmd   = 0;
    uint16_t   m_frameLen   = 0;
    uint16_t   m_frameIdx   = 0;
    QByteArray m_frameData;

    // 下载状态
    DlStep     m_dlStep   = DlStep::Idle;
    QByteArray m_binData;
    int        m_dlPage   = 0;
    int        m_dlTotal  = 0;
    bool       m_aborting = false;

    static constexpr uint8_t SOF = 0xAA;
    static constexpr uint8_t ACK = 0x06;
    static constexpr uint8_t NAK = 0x15;

    static uint8_t    crc8(const QByteArray& data);
    QByteArray        buildFrame(uint8_t cmd, const QByteArray& payload = {});
    void              sendFrame(uint8_t cmd, const QByteArray& payload = {});
    void              armTimeout(int ms);

    void onDataReceived(const QByteArray& data);
    void onResponse(bool isAck, uint8_t cmd, const QByteArray& data);
    void onTimeout();
    void startNextPage();
    void fail(const QString& reason);
};
