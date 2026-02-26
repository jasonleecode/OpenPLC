#pragma once
#include "IPlcTransport.h"
#include <QSerialPort>

// ─────────────────────────────────────────────────────────────────────────────
// SerialTransport — 基于 QSerialPort 的串口传输
// ─────────────────────────────────────────────────────────────────────────────
class SerialTransport : public IPlcTransport {
    Q_OBJECT
public:
    explicit SerialTransport(QObject* parent = nullptr);
    ~SerialTransport() override;

    void setPort(const QString& portName);
    void setBaudRate(int baudRate);

    // 返回当前系统可用串口名列表
    static QStringList availablePorts();

    bool    open()                        override;
    void    close()                       override;
    bool    isOpen()            const     override;
    bool    write(const QByteArray& data) override;
    QString displayName()       const     override;

private:
    QSerialPort* m_serial;
    QString      m_portName;
    int          m_baudRate = 115200;
};
