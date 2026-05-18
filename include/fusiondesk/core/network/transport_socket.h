#ifndef FUSIONDESK_NETWORK_TRANSPORT_SOCKET_H
#define FUSIONDESK_NETWORK_TRANSPORT_SOCKET_H

#include <string>

#include "fusiondesk/core/network/channel.h"

namespace fusiondesk {
namespace network {

enum class SocketState
{
    Closed,
    Open,
    Failed
};

struct SocketOpenOptions
{
    SocketClass socketClass = SocketClass::Control;
    std::string endpoint;
};

struct CloseReason
{
    std::string message;
};

class ITransportSocket
{
public:
    virtual ~ITransportSocket() = default;

    virtual SocketClass socketClass() const = 0;
    virtual bool open(const SocketOpenOptions& options) = 0;
    virtual void close(const CloseReason& reason) = 0;
    virtual SendResult write(const protocol::ByteBuffer& bytes) = 0;
    virtual SocketState state() const = 0;
};

} // namespace network
} // namespace fusiondesk

#endif // FUSIONDESK_NETWORK_TRANSPORT_SOCKET_H
