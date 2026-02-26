#include "TcpTransport.h"

TcpTransport::TcpTransport(QObject* parent)
    : IPlcTransport(parent)
    , m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::readyRead, this, [this] {
        emit dataReceived(m_socket->readAll());
    });
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, [this](QAbstractSocket::SocketError) {
        emit errorOccurred(m_socket->errorString());
    });
}

TcpTransport::~TcpTransport()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->disconnectFromHost();
}

void TcpTransport::setHost(const QString& host) { m_host = host; }
void TcpTransport::setPort(int port)             { m_port = port; }

bool TcpTransport::open()
{
    m_socket->connectToHost(m_host, static_cast<quint16>(m_port));
    return m_socket->waitForConnected(5000);
}

void TcpTransport::close()
{
    m_socket->disconnectFromHost();
}

bool TcpTransport::isOpen() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

bool TcpTransport::write(const QByteArray& data)
{
    return m_socket->write(data) == static_cast<qint64>(data.size());
}

QString TcpTransport::displayName() const
{
    return QString("TCP(%1:%2)").arg(m_host).arg(m_port);
}
