#include "fusiondesk/core/network/socket_group.h"

#include <utility>

namespace fusiondesk {
namespace network {

SocketGroupResult SocketGroup::registerSocket(std::shared_ptr<ITransportSocket> socket)
{
    if (!socket) {
        return SocketGroupResult::failed(SocketGroupStatus::InvalidArgument,
                                         "transport socket is required");
    }

    const SocketClass socketClass = socket->socketClass();
    if (sockets_.find(socketClass) != sockets_.end()) {
        return SocketGroupResult::failed(SocketGroupStatus::AlreadyRegistered,
                                         "transport socket class already registered");
    }

    Entry entry;
    entry.socket = std::move(socket);
    sockets_[socketClass] = std::move(entry);
    return SocketGroupResult::success();
}

SocketGroupResult SocketGroup::open(SocketClass socketClass, const SocketOpenOptions& options)
{
    auto it = sockets_.find(socketClass);
    if (it == sockets_.end()) {
        return SocketGroupResult::failed(SocketGroupStatus::NotFound,
                                         "transport socket class not registered");
    }

    if (!it->second.socket->open(options)) {
        it->second.message = "transport socket open failed";
        return SocketGroupResult::failed(SocketGroupStatus::Failed, it->second.message);
    }

    it->second.message.clear();
    return SocketGroupResult::success();
}

void SocketGroup::close(SocketClass socketClass, const CloseReason& reason)
{
    auto it = sockets_.find(socketClass);
    if (it == sockets_.end())
        return;

    it->second.socket->close(reason);
    it->second.message = reason.message;
}

SocketGroupResult SocketGroup::write(SocketClass socketClass, const protocol::ByteBuffer& bytes)
{
    auto it = sockets_.find(socketClass);
    if (it == sockets_.end()) {
        return SocketGroupResult::failed(SocketGroupStatus::NotFound,
                                         "transport socket class not registered");
    }

    SendResult result = it->second.socket->write(bytes);
    if (result.status != SendStatus::Sent) {
        it->second.message = result.message;
        return SocketGroupResult::failed(SocketGroupStatus::Failed, result.message);
    }

    return SocketGroupResult::success();
}

std::shared_ptr<ITransportSocket> SocketGroup::socket(SocketClass socketClass) const
{
    auto it = sockets_.find(socketClass);
    if (it == sockets_.end())
        return {};
    return it->second.socket;
}

SocketSnapshot SocketGroup::snapshot(SocketClass socketClass) const
{
    auto it = sockets_.find(socketClass);
    if (it == sockets_.end())
        return {};

    SocketSnapshot snapshot;
    snapshot.socketClass = socketClass;
    snapshot.state = it->second.socket->state();
    snapshot.message = it->second.message;
    return snapshot;
}

std::vector<SocketSnapshot> SocketGroup::snapshots() const
{
    std::vector<SocketSnapshot> result;
    result.reserve(sockets_.size());
    for (const auto& item : sockets_)
        result.push_back(snapshot(item.first));
    return result;
}

} // namespace network
} // namespace fusiondesk
