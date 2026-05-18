#ifndef FUSIONDESK_ADAPTERS_QT_QT_TCP_TRANSPORT_SOCKET_H
#define FUSIONDESK_ADAPTERS_QT_QT_TCP_TRANSPORT_SOCKET_H

#include <functional>
#include <memory>

#include "fusiondesk/core/network/transport_socket.h"

class QTcpSocket;

namespace fusiondesk {
namespace adapters {
namespace qt {

class QtTcpTransportSocket : public network::ITransportSocket
{
public:
    using BytesReceivedHandler = std::function<void(protocol::ByteBuffer)>;

    explicit QtTcpTransportSocket(network::SocketClass socketClass);
    ~QtTcpTransportSocket() override;

    QtTcpTransportSocket(const QtTcpTransportSocket&) = delete;
    QtTcpTransportSocket& operator=(const QtTcpTransportSocket&) = delete;

    static std::shared_ptr<QtTcpTransportSocket> adopt(network::SocketClass socketClass,
                                                       QTcpSocket* socket);

    void setBytesReceivedHandler(BytesReceivedHandler handler);
    void poll();

    network::SocketClass socketClass() const override;
    bool open(const network::SocketOpenOptions& options) override;
    void close(const network::CloseReason& reason) override;
    network::SendResult write(const protocol::ByteBuffer& bytes) override;
    network::SocketState state() const override;

private:
    struct Impl;
    explicit QtTcpTransportSocket(std::unique_ptr<Impl> impl);

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace qt
} // namespace adapters
} // namespace fusiondesk

#endif // FUSIONDESK_ADAPTERS_QT_QT_TCP_TRANSPORT_SOCKET_H
