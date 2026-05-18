#include "fusiondesk/adapters/qt/qt_tcp_transport_socket.h"

#include <cstdlib>
#include <string>
#include <utility>

#include <QAbstractSocket>
#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QTcpSocket>

namespace fusiondesk {
namespace adapters {
namespace qt {

namespace {

constexpr int DefaultConnectTimeoutMs = 3000;
constexpr int DefaultWriteTimeoutMs = 3000;

bool parseEndpoint(const std::string& endpoint, QString& host, quint16& port)
{
    std::string value = endpoint;
    const std::string prefix = "tcp://";
    if (value.rfind(prefix, 0) == 0)
        value = value.substr(prefix.size());

    const std::size_t delimiter = value.rfind(':');
    if (delimiter == std::string::npos || delimiter == 0 || delimiter + 1 >= value.size())
        return false;

    char* end = nullptr;
    const long parsedPort = std::strtol(value.c_str() + delimiter + 1, &end, 10);
    if (end == nullptr || *end != '\0' || parsedPort <= 0 || parsedPort > 65535)
        return false;

    host = QString::fromStdString(value.substr(0, delimiter));
    port = static_cast<quint16>(parsedPort);
    return !host.isEmpty();
}

network::SocketState stateFromSocket(const QTcpSocket* socket)
{
    if (socket == nullptr)
        return network::SocketState::Closed;

    if (socket->state() == QAbstractSocket::ConnectedState)
        return network::SocketState::Open;

    if (socket->state() == QAbstractSocket::UnconnectedState)
        return network::SocketState::Closed;

    return network::SocketState::Failed;
}

} // namespace

struct QtTcpTransportSocket::Impl
{
    explicit Impl(network::SocketClass socketClassIn)
        : socketClass(socketClassIn)
    {
    }

    void setSocket(std::unique_ptr<QTcpSocket> nextSocket)
    {
        socket = std::move(nextSocket);
        connectSignals();
        state = stateFromSocket(socket.get());
    }

    void connectSignals()
    {
        if (!socket)
            return;

        QObject::connect(socket.get(), &QTcpSocket::readyRead, socket.get(), [this]() {
            drainIncoming();
        });
        QObject::connect(socket.get(), &QTcpSocket::disconnected, socket.get(), [this]() {
            state = network::SocketState::Closed;
        });
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        QObject::connect(socket.get(), &QTcpSocket::errorOccurred, socket.get(), [this]() {
            state = network::SocketState::Failed;
        });
#else
        QObject::connect(socket.get(),
                         QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
                         socket.get(),
                         [this]() {
                             state = network::SocketState::Failed;
                         });
#endif
    }

    void drainIncoming()
    {
        if (!socket || !bytesReceivedHandler)
            return;

        const QByteArray bytes = socket->readAll();
        if (bytes.isEmpty())
            return;

        const auto* first = reinterpret_cast<const std::uint8_t*>(bytes.constData());
        protocol::ByteBuffer buffer(first, first + bytes.size());
        bytesReceivedHandler(std::move(buffer));
    }

    network::SocketClass socketClass = network::SocketClass::Control;
    std::unique_ptr<QTcpSocket> socket;
    network::SocketState state = network::SocketState::Closed;
    BytesReceivedHandler bytesReceivedHandler;
};

QtTcpTransportSocket::QtTcpTransportSocket(network::SocketClass socketClass)
    : impl_(std::make_unique<Impl>(socketClass))
{
}

QtTcpTransportSocket::QtTcpTransportSocket(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}

QtTcpTransportSocket::~QtTcpTransportSocket()
{
    close({"destroying qt tcp transport socket"});
}

std::shared_ptr<QtTcpTransportSocket> QtTcpTransportSocket::adopt(network::SocketClass socketClass,
                                                                  QTcpSocket* socket)
{
    if (socket != nullptr)
        socket->setParent(nullptr);

    auto impl = std::make_unique<Impl>(socketClass);
    impl->setSocket(std::unique_ptr<QTcpSocket>(socket));
    return std::shared_ptr<QtTcpTransportSocket>(new QtTcpTransportSocket(std::move(impl)));
}

void QtTcpTransportSocket::setBytesReceivedHandler(BytesReceivedHandler handler)
{
    impl_->bytesReceivedHandler = std::move(handler);
}

void QtTcpTransportSocket::poll()
{
    if (impl_->socket)
        impl_->drainIncoming();

    if (QCoreApplication::instance() != nullptr)
        QCoreApplication::processEvents();
}

network::SocketClass QtTcpTransportSocket::socketClass() const
{
    return impl_->socketClass;
}

bool QtTcpTransportSocket::open(const network::SocketOpenOptions& options)
{
    QString host;
    quint16 port = 0;
    if (!parseEndpoint(options.endpoint, host, port)) {
        impl_->state = network::SocketState::Failed;
        return false;
    }

    auto socket = std::make_unique<QTcpSocket>();
    socket->connectToHost(host, port);
    if (!socket->waitForConnected(DefaultConnectTimeoutMs)) {
        impl_->state = network::SocketState::Failed;
        return false;
    }

    impl_->setSocket(std::move(socket));
    impl_->state = network::SocketState::Open;
    return true;
}

void QtTcpTransportSocket::close(const network::CloseReason&)
{
    if (!impl_->socket) {
        impl_->state = network::SocketState::Closed;
        return;
    }

    if (impl_->socket->state() != QAbstractSocket::UnconnectedState) {
        impl_->socket->disconnectFromHost();
        if (impl_->socket->state() != QAbstractSocket::UnconnectedState)
            impl_->socket->waitForDisconnected(100);
    }
    impl_->state = network::SocketState::Closed;
}

network::SendResult QtTcpTransportSocket::write(const protocol::ByteBuffer& bytes)
{
    if (!impl_->socket || impl_->socket->state() != QAbstractSocket::ConnectedState) {
        impl_->state = network::SocketState::Closed;
        return {network::SendStatus::ChannelClosed, "qt tcp socket is not connected"};
    }

    const QByteArray payload(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<int>(bytes.size()));
    const qint64 written = impl_->socket->write(payload);
    if (written != payload.size()) {
        impl_->state = network::SocketState::Failed;
        return {network::SendStatus::Failed, "qt tcp socket did not accept the full payload"};
    }

    if (!impl_->socket->waitForBytesWritten(DefaultWriteTimeoutMs)) {
        impl_->state = network::SocketState::Failed;
        return {network::SendStatus::Failed, "qt tcp socket write timed out"};
    }

    return network::SendResult::sent();
}

network::SocketState QtTcpTransportSocket::state() const
{
    return impl_->state;
}

} // namespace qt
} // namespace adapters
} // namespace fusiondesk
