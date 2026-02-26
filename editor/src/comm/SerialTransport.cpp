#include "SerialTransport.h"
#include <QSerialPortInfo>

SerialTransport::SerialTransport(QObject* parent)
    : IPlcTransport(parent)
    , m_serial(new QSerialPort(this))
{
    connect(m_serial, &QSerialPort::readyRead, this, [this] {
        emit dataReceived(m_serial->readAll());
    });
    connect(m_serial, &QSerialPort::errorOccurred,
            this, [this](QSerialPort::SerialPortError e) {
        if (e != QSerialPort::NoError)
            emit errorOccurred(m_serial->errorString());
    });
}

SerialTransport::~SerialTransport()
{
    if (m_serial->isOpen()) m_serial->close();
}

void SerialTransport::setPort(const QString& portName) { m_portName = portName; }
void SerialTransport::setBaudRate(int baudRate)         { m_baudRate = baudRate; }

QStringList SerialTransport::availablePorts()
{
    QStringList list;
    for (const auto& info : QSerialPortInfo::availablePorts())
        list << info.portName();
    return list;
}

bool SerialTransport::open()
{
    m_serial->setPortName(m_portName);
    m_serial->setBaudRate(m_baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);
    return m_serial->open(QIODevice::ReadWrite);
}

void SerialTransport::close()
{
    if (m_serial->isOpen()) m_serial->close();
}

bool SerialTransport::isOpen() const { return m_serial->isOpen(); }

bool SerialTransport::write(const QByteArray& data)
{
    return m_serial->write(data) == static_cast<qint64>(data.size());
}

QString SerialTransport::displayName() const
{
    return QString("Serial(%1 @ %2)").arg(m_portName).arg(m_baudRate);
}
