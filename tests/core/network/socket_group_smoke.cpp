#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/network/socket_group.h"

using namespace fusiondesk;

namespace {

class FakeTransportSocket : public network::ITransportSocket
{
public:
    explicit FakeTransportSocket(network::SocketClass socketClass)
        : socketClass_(socketClass)
    {
    }

    network::SocketClass socketClass() const override
    {
        return socketClass_;
    }

    bool open(const network::SocketOpenOptions& options) override
    {
        endpoint_ = options.endpoint;
        state_ = network::SocketState::Open;
        return true;
    }

    void close(const network::CloseReason&) override
    {
        state_ = network::SocketState::Closed;
    }

    network::SendResult write(const protocol::ByteBuffer& bytes) override
    {
        if (state_ != network::SocketState::Open)
            return {network::SendStatus::ChannelClosed, "socket is not open"};

        writes_.push_back(bytes);
        return network::SendResult::sent();
    }

    network::SocketState state() const override
    {
        return state_;
    }

    const std::vector<protocol::ByteBuffer>& writes() const
    {
        return writes_;
    }

private:
    network::SocketClass socketClass_ = network::SocketClass::Control;
    network::SocketState state_ = network::SocketState::Closed;
    std::string endpoint_;
    std::vector<protocol::ByteBuffer> writes_;
};

void registersOpensAndWritesSocket()
{
    network::SocketGroup group;
    auto control = std::make_shared<FakeTransportSocket>(network::SocketClass::Control);

    assert(group.registerSocket(control).ok);

    network::SocketOpenOptions options;
    options.socketClass = network::SocketClass::Control;
    options.endpoint = "control";
    assert(group.open(network::SocketClass::Control, options).ok);
    assert(group.snapshot(network::SocketClass::Control).state == network::SocketState::Open);

    assert(group.write(network::SocketClass::Control, {1, 2, 3}).ok);
    assert(control->writes().size() == 1);
}

void failureIsIsolatedToOneSocketClass()
{
    network::SocketGroup group;
    auto control = std::make_shared<FakeTransportSocket>(network::SocketClass::Control);
    auto realtime = std::make_shared<FakeTransportSocket>(network::SocketClass::Realtime);
    assert(group.registerSocket(control).ok);
    assert(group.registerSocket(realtime).ok);

    network::SocketOpenOptions controlOptions;
    controlOptions.socketClass = network::SocketClass::Control;
    controlOptions.endpoint = "control";
    network::SocketOpenOptions realtimeOptions;
    realtimeOptions.socketClass = network::SocketClass::Realtime;
    realtimeOptions.endpoint = "realtime";
    assert(group.open(network::SocketClass::Control, controlOptions).ok);
    assert(group.open(network::SocketClass::Realtime, realtimeOptions).ok);

    group.close(network::SocketClass::Realtime, {"realtime failed"});

    assert(group.snapshot(network::SocketClass::Realtime).state == network::SocketState::Closed);
    assert(group.snapshot(network::SocketClass::Control).state == network::SocketState::Open);
    assert(group.write(network::SocketClass::Control, {4}).ok);
    assert(!group.write(network::SocketClass::Realtime, {5}).ok);
}

void rejectsDuplicateSocketClass()
{
    network::SocketGroup group;
    assert(group.registerSocket(std::make_shared<FakeTransportSocket>(network::SocketClass::Bulk)).ok);
    assert(group.registerSocket(std::make_shared<FakeTransportSocket>(network::SocketClass::Bulk)).status ==
           network::SocketGroupStatus::AlreadyRegistered);
}

} // namespace

int main()
{
    registersOpensAndWritesSocket();
    failureIsIsolatedToOneSocketClass();
    rejectsDuplicateSocketClass();
    return 0;
}
