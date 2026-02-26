#include "PlcProtocol.h"
#include "IPlcTransport.h"
#include <QTimer>

PlcProtocol::PlcProtocol(IPlcTransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
    , m_timeoutTimer(new QTimer(this))
{
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &PlcProtocol::onTimeout);
    connect(m_transport,    &IPlcTransport::dataReceived,
            this, &PlcProtocol::onDataReceived);
}

// ─────────────────────────────────────────────────────────────────────────────
// CRC-8/MAXIM (poly 0x31, init 0x00) — 与 runtime 保持一致
// ─────────────────────────────────────────────────────────────────────────────
uint8_t PlcProtocol::crc8(const QByteArray& data)
{
    uint8_t crc = 0;
    for (uint8_t b : data) {
        crc ^= b;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x80u) ? static_cast<uint8_t>((crc << 1u) ^ 0x31u)
                                : static_cast<uint8_t>(crc << 1u);
    }
    return crc;
}

// ─────────────────────────────────────────────────────────────────────────────
// 帧构建
// ─────────────────────────────────────────────────────────────────────────────
QByteArray PlcProtocol::buildFrame(uint8_t cmd, const QByteArray& payload)
{
    QByteArray frame;
    frame.reserve(4 + payload.size() + 1);
    frame.append(static_cast<char>(SOF));
    frame.append(static_cast<char>(cmd));
    auto len = static_cast<uint16_t>(payload.size());
    frame.append(static_cast<char>(len & 0xFFu));
    frame.append(static_cast<char>(len >> 8u));
    frame.append(payload);
    frame.append(static_cast<char>(crc8(payload)));
    return frame;
}

void PlcProtocol::sendFrame(uint8_t cmd, const QByteArray& payload)
{
    m_transport->write(buildFrame(cmd, payload));
}

void PlcProtocol::armTimeout(int ms) { m_timeoutTimer->start(ms); }

// ─────────────────────────────────────────────────────────────────────────────
// 公开接口
// ─────────────────────────────────────────────────────────────────────────────
void PlcProtocol::downloadBinary(const QByteArray& bin)
{
    if (m_dlStep != DlStep::Idle) return;

    m_binData = bin;
    // 末尾补 0xFF 对齐到 PAGE_SIZE
    while (m_binData.size() % static_cast<int>(FLASH_PAGE_SIZE) != 0)
        m_binData.append('\xFF');

    m_dlPage   = 0;
    m_dlTotal  = m_binData.size() / static_cast<int>(FLASH_PAGE_SIZE);
    m_aborting = false;
    m_dlStep   = DlStep::Ping;

    emit logMessage(QString("Starting download: %1 bytes → %2 pages")
                    .arg(bin.size()).arg(m_dlTotal));

    sendFrame(CMD_PING);
    armTimeout(3000);
}

void PlcProtocol::abort()
{
    m_aborting = true;
    m_timeoutTimer->stop();
    m_dlStep = DlStep::Idle;
    emit logMessage("Download aborted by user.");
}

void PlcProtocol::sendPing()
{
    sendFrame(CMD_PING);
    armTimeout(3000);
}

void PlcProtocol::sendGetStatus()
{
    sendFrame(CMD_GET_STATUS);
    armTimeout(2000);
}

void PlcProtocol::sendSetRun(bool run)
{
    QByteArray p(1, run ? '\x01' : '\x00');
    sendFrame(CMD_SET_RUN, p);
    armTimeout(2000);
}

void PlcProtocol::sendReadIo()
{
    sendFrame(CMD_READ_IO);
    armTimeout(2000);
}

// ─────────────────────────────────────────────────────────────────────────────
// 响应帧解析状态机
// 接收到的字节流可能被拆分，逐字节处理
// ─────────────────────────────────────────────────────────────────────────────
void PlcProtocol::onDataReceived(const QByteArray& data)
{
    for (char ch : data) {
        auto byte = static_cast<uint8_t>(ch);

        switch (m_parseState) {

        case ParseState::WaitFirst:
            if (byte == ACK) {
                onResponse(true, 0, {});
            } else if (byte == NAK) {
                onResponse(false, 0, {});
            } else if (byte == SOF) {
                m_frameData.clear();
                m_parseState = ParseState::FrameCmd;
            }
            // 其他字节忽略（噪声）
            break;

        case ParseState::FrameCmd:
            m_frameCmd   = byte;
            m_parseState = ParseState::FrameLenLo;
            break;

        case ParseState::FrameLenLo:
            m_frameLen   = byte;
            m_parseState = ParseState::FrameLenHi;
            break;

        case ParseState::FrameLenHi:
            m_frameLen |= static_cast<uint16_t>(static_cast<uint16_t>(byte) << 8u);
            m_frameIdx  = 0;
            m_frameData.clear();
            m_parseState = (m_frameLen > 0) ? ParseState::FrameData
                                            : ParseState::FrameCrc;
            break;

        case ParseState::FrameData:
            m_frameData.append(static_cast<char>(byte));
            if (++m_frameIdx >= m_frameLen)
                m_parseState = ParseState::FrameCrc;
            break;

        case ParseState::FrameCrc: {
            m_parseState = ParseState::WaitFirst;
            if (byte == crc8(m_frameData))
                onResponse(true, m_frameCmd, m_frameData);
            else
                emit logMessage("[WARN] CRC mismatch in response frame");
            break;
        }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 响应处理 — 驱动下载状态机
// ─────────────────────────────────────────────────────────────────────────────
void PlcProtocol::onResponse(bool isAck, uint8_t cmd, const QByteArray& data)
{
    m_timeoutTimer->stop();

    // ── 非下载状态：处理运行时控制命令的响应 ──────────────────
    if (m_dlStep == DlStep::Idle) {
        if (cmd == CMD_GET_STATUS && isAck && data.size() >= 5) {
            bool running = static_cast<uint8_t>(data[0]) != 0;
            uint32_t t = static_cast<uint8_t>(data[1])
                       | (static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 8u)
                       | (static_cast<uint32_t>(static_cast<uint8_t>(data[3])) << 16u)
                       | (static_cast<uint32_t>(static_cast<uint8_t>(data[4])) << 24u);
            emit statusResponse(running, t);
        } else if (cmd == CMD_PING && isAck) {
            emit pingResponse(QString::fromLatin1(data));
        } else if (cmd == CMD_READ_IO && isAck && data.size() >= 2) {
            emit ioResponse(static_cast<uint8_t>(data[0]),
                            static_cast<uint8_t>(data[1]));
        }
        return;
    }

    // ── 下载状态机 ───────────────────────────────────────────
    if (!isAck) { fail("NAK received from device"); return; }
    if (m_aborting) { fail("Aborted"); return; }

    switch (m_dlStep) {

    case DlStep::Ping: {
        QString ver = (cmd == CMD_PING && !data.isEmpty())
                      ? QString::fromLatin1(data)
                      : "PLC";
        emit logMessage(QString("Connected: %1").arg(ver));
        emit pingResponse(ver);

        m_dlStep = DlStep::Erase;
        emit logMessage("Erasing user flash (sectors 16-31)...");
        sendFrame(CMD_ERASE);
        armTimeout(8000);   // 擦除最多需要 ~3s/sector × 16 sectors
        break;
    }

    case DlStep::Erase:
        emit logMessage("Erase OK.");
        m_dlStep = DlStep::Write;
        m_dlPage = 0;
        startNextPage();
        break;

    case DlStep::Write: {
        emit downloadProgress(m_dlPage + 1, m_dlTotal);
        m_dlPage++;

        if (m_dlPage >= m_dlTotal) {
            // 全部页写完，发校验命令
            m_dlStep = DlStep::Verify;

            QByteArray vp(7, '\0');
            uint32_t addr = USER_FLASH_BASE;
            uint32_t len  = static_cast<uint32_t>(m_binData.size());
            vp[0] = static_cast<char>(addr & 0xFFu);
            vp[1] = static_cast<char>((addr >> 8u) & 0xFFu);
            vp[2] = static_cast<char>((addr >> 16u) & 0xFFu);
            vp[3] = static_cast<char>((addr >> 24u) & 0xFFu);
            vp[4] = static_cast<char>(len & 0xFFu);
            vp[5] = static_cast<char>((len >> 8u) & 0xFFu);
            vp[6] = static_cast<char>(crc8(m_binData));

            emit logMessage("Verifying...");
            sendFrame(CMD_VERIFY, vp);
            armTimeout(4000);
        } else {
            startNextPage();
        }
        break;
    }

    case DlStep::Verify:
        emit logMessage("Verify OK. Resetting PLC...");
        m_dlStep = DlStep::Reset;
        sendFrame(CMD_RESET);
        armTimeout(2000);
        break;

    case DlStep::Reset:
        m_dlStep = DlStep::Idle;
        emit logMessage("Download complete! PLC restarted.");
        emit downloadComplete();
        break;

    default:
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 写入下一页
// ─────────────────────────────────────────────────────────────────────────────
void PlcProtocol::startNextPage()
{
    if (m_aborting) { fail("Aborted"); return; }

    uint32_t addr = USER_FLASH_BASE
                  + static_cast<uint32_t>(m_dlPage) * FLASH_PAGE_SIZE;
    QByteArray page = m_binData.mid(m_dlPage * static_cast<int>(FLASH_PAGE_SIZE),
                                    static_cast<int>(FLASH_PAGE_SIZE));

    // 构造载荷：[addr:4LE][data:256]
    QByteArray payload(4 + static_cast<int>(FLASH_PAGE_SIZE), '\0');
    payload[0] = static_cast<char>(addr & 0xFFu);
    payload[1] = static_cast<char>((addr >> 8u)  & 0xFFu);
    payload[2] = static_cast<char>((addr >> 16u) & 0xFFu);
    payload[3] = static_cast<char>((addr >> 24u) & 0xFFu);
    payload.replace(4, static_cast<int>(FLASH_PAGE_SIZE), page);

    emit logMessage(QString("  Page %1/%2 → 0x%3")
                    .arg(m_dlPage + 1).arg(m_dlTotal)
                    .arg(addr, 8, 16, QChar('0')));

    sendFrame(CMD_WRITE_PAGE, payload);
    armTimeout(3000);
}

void PlcProtocol::onTimeout()
{
    fail(QString("Timeout waiting for response (step %1)")
         .arg(static_cast<int>(m_dlStep)));
}

void PlcProtocol::fail(const QString& reason)
{
    m_dlStep = DlStep::Idle;
    m_timeoutTimer->stop();
    emit logMessage(QString("[ERROR] %1").arg(reason));
    emit downloadFailed(reason);
}
