#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
// IPlcTransport — 传输层纯虚接口
//
// 所有传输方式（串口、TCP、USB…）实现此接口。
// PlcProtocol 只依赖此接口，无需关心底层传输细节。
// ─────────────────────────────────────────────────────────────────────────────
class IPlcTransport : public QObject {
    Q_OBJECT
public:
    explicit IPlcTransport(QObject* parent = nullptr) : QObject(parent) {}
    ~IPlcTransport() override = default;

    virtual bool    open()                          = 0;
    virtual void    close()                         = 0;
    virtual bool    isOpen()              const     = 0;
    virtual bool    write(const QByteArray& data)   = 0;
    virtual QString displayName()         const     = 0;

signals:
    void dataReceived(const QByteArray& data);
    void errorOccurred(const QString& msg);
};
