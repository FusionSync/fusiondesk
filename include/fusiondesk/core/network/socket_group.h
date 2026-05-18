#ifndef FUSIONDESK_NETWORK_SOCKET_GROUP_H
#define FUSIONDESK_NETWORK_SOCKET_GROUP_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/network/transport_socket.h"

namespace fusiondesk {
namespace network {

struct SocketSnapshot
{
    SocketClass socketClass = SocketClass::Control;
    SocketState state = SocketState::Closed;
    std::string message;
};

enum class SocketGroupStatus
{
    Ok,
    AlreadyRegistered,
    NotFound,
    InvalidArgument,
    Failed
};

struct SocketGroupResult
{
    bool ok = false;
    SocketGroupStatus status = SocketGroupStatus::Failed;
    std::string message;

    static SocketGroupResult success()
    {
        return {true, SocketGroupStatus::Ok, {}};
    }

    static SocketGroupResult failed(SocketGroupStatus status, std::string message)
    {
        return {false, status, message};
    }
};

class SocketGroup
{
public:
    SocketGroupResult registerSocket(std::shared_ptr<ITransportSocket> socket);
    SocketGroupResult open(SocketClass socketClass, const SocketOpenOptions& options);
    void close(SocketClass socketClass, const CloseReason& reason);
    SocketGroupResult write(SocketClass socketClass, const protocol::ByteBuffer& bytes);
    std::shared_ptr<ITransportSocket> socket(SocketClass socketClass) const;
    SocketSnapshot snapshot(SocketClass socketClass) const;
    std::vector<SocketSnapshot> snapshots() const;

private:
    struct Entry
    {
        std::shared_ptr<ITransportSocket> socket;
        std::string message;
    };

private:
    std::map<SocketClass, Entry> sockets_;
};

} // namespace network
} // namespace fusiondesk

#endif // FUSIONDESK_NETWORK_SOCKET_GROUP_H
