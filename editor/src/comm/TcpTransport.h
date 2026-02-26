#pragma once
#include "IPlcTransport.h"
#include <QTcpSocket>

// ─────────────────────────────────────────────────────────────────────────────
// TcpTransport — 基于 QTcpSocket 的以太网传输
//
// 现阶段为占位实现，用于将来以太网下载扩展。
// 协议帧格式与串口完全相同，传输层透明切换。
// ─────────────────────────────────────────────────────────────────────────────
class TcpTransport : public IPlcTransport {
    Q_OBJECT
public:
    explicit TcpTransport(QObject* parent = nullptr);
    ~TcpTransport() override;

    void setHost(const QString& host);
    void setPort(int port);

    bool    open()                        override;
    void    close()                       override;
    bool    isOpen()            const     override;
    bool    write(const QByteArray& data) override;
    QString displayName()       const     override;

private:
    QTcpSocket* m_socket;
    QString     m_host = "192.168.1.100";
    int         m_port = 6699;
};
